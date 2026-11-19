#include "llm_client.h"

#include <windows.h>
#include <winhttp.h>

#include <cctype>
#include <fstream>
#include <sstream>
#include <thread>

namespace mumpf {

namespace {

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

std::string EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20) out.push_back(c);
                break;
        }
    }
    return out;
}

bool ExtractFirstJsonString(const std::string& json, const std::string& key, std::string& out, size_t startPos = 0) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, startPos);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != '"') return false;
    ++pos;

    std::string res;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '\\' && pos < json.size()) {
            char n = json[pos++];
            switch (n) {
                case '"':  res.push_back('"'); break;
                case '\\': res.push_back('\\'); break;
                case 'n':  res.push_back('\n'); break;
                case 'r':  res.push_back('\r'); break;
                case 't':  res.push_back('\t'); break;
                default:   res.push_back(n); break;
            }
        } else if (c == '"') {
            out = std::move(res);
            return true;
        } else {
            res.push_back(c);
        }
    }
    return false;
}

bool ExtractJsonInt(const std::string& json, const std::string& key, int& out) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    size_t end = pos;
    while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end]))) ++end;
    if (end == pos) return false;
    try {
        out = std::stoi(json.substr(pos, end - pos));
        return true;
    } catch (...) {
        return false;
    }
}

bool ExtractJsonBool(const std::string& json, const std::string& key, bool& out) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (json.compare(pos, 4, "true") == 0) { out = true; return true; }
    if (json.compare(pos, 5, "false") == 0) { out = false; return true; }
    return false;
}

std::string DetectLoadedModel(HINTERNET hConnect) {
    HINTERNET hReq = WinHttpOpenRequest(
        hConnect, L"GET", L"/v1/models", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hReq) return {};

    std::string response;
    if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (WinHttpReadData(hReq, chunk.data(), avail, &read) && read > 0) {
                response.append(chunk.data(), read);
            } else {
                break;
            }
        }
    }
    WinHttpCloseHandle(hReq);

    size_t dataPos = response.find("\"data\"");
    if (dataPos == std::string::npos) return {};

    // Erstes Modell in "data" auslesen (Embedding-Modelle überspringen, falls möglich)
    size_t searchPos = dataPos;
    std::string fallbackModel;
    while (true) {
        std::string id;
        if (!ExtractFirstJsonString(response, "id", id, searchPos)) break;
        if (fallbackModel.empty()) fallbackModel = id;
        if (id.find("embed") == std::string::npos && id.find("Embed") == std::string::npos) {
            return id;
        }
        size_t nextPos = response.find(id, searchPos);
        if (nextPos == std::string::npos) break;
        searchPos = nextPos + id.size();
    }
    return fallbackModel;
}

} // namespace

LlmConfig LlmClient::LoadConfigFromFile(const std::wstring& path) {
    LlmConfig cfg;
    std::ifstream ifs(path);
    if (!ifs.is_open()) return cfg;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    const std::string json = oss.str();

    ExtractJsonBool(json, "llm_enabled", cfg.enabled);
    std::string host;
    if (ExtractFirstJsonString(json, "llm_host", host)) {
        cfg.host = Utf8ToWide(host);
    }
    ExtractJsonInt(json, "llm_port", cfg.port);
    std::string model;
    if (ExtractFirstJsonString(json, "llm_model", model)) {
        cfg.model = model;
    }
    return cfg;
}

void LlmClient::AskAsync(
    const std::wstring& contextTitle,
    const std::wstring& contextBody,
    const std::wstring& customInstruction,
    ReplyCallback onReply) const {
    if (!config_.enabled || !onReply) return;

    LlmConfig cfg = config_;
    std::string instr = customInstruction.empty()
        ? "Erklaere kurz und praxisnah auf Deutsch (maximal 2-3 kurze Saetze), was das Problem ist und was der naechste sinnvolle Schritt ist."
        : WideToUtf8(customInstruction);

    std::string userPrompt = "Kontext: " + WideToUtf8(contextTitle) + "\nText:\n" + WideToUtf8(contextBody) +
        "\n\nAufgabe: " + instr + "\nAntworte kurz, klar und auf Deutsch (kein Markdown).";

    std::thread([cfg, userPrompt, onReply = std::move(onReply)]() {
        const std::string systemPrompt =
            "Du bist Mumpf, ein freundliches, leicht trockenes Windows-11-Entwickler-Helferlein. "
            "Antworte immer auf Deutsch, praezise, hilfsbereit und extrem kompakt (maximal 3 kurze Saetze, kein Markdown).";

        HINTERNET hSession = WinHttpOpen(L"MumpfWindows/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
        if (!hSession) {
            onReply(L"(LM Studio nicht erreichbar)", false);
            return;
        }

        WinHttpSetTimeouts(hSession, 3000, 5000, 10000, 90000);

        HINTERNET hConnect = WinHttpConnect(hSession, cfg.host.c_str(), static_cast<INTERNET_PORT>(cfg.port), 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            onReply(L"(LM Studio Server unter " + cfg.host + L" nicht erreichbar)", false);
            return;
        }

        // Automatisch das aktuell in LM Studio geladene Modell erkennen
        std::string activeModel = cfg.model;
        if (activeModel.empty() || activeModel == "auto") {
            std::string detected = DetectLoadedModel(hConnect);
            if (!detected.empty()) {
                activeModel = detected;
            }
        }
        if (activeModel.empty() || activeModel == "auto") {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            onReply(L"(Kein Modell in LM Studio geladen – bitte zuerst ein Modell laden.)", false);
            return;
        }

        const std::string body =
            "{\"model\":\"" + EscapeJson(activeModel) + "\","
            "\"temperature\":0.3,"
            "\"max_tokens\":600,"
            "\"messages\":["
                "{\"role\":\"system\",\"content\":\"" + EscapeJson(systemPrompt) + "\"},"
                "{\"role\":\"user\",\"content\":\"" + EscapeJson(userPrompt) + "\"}"
            "]}";

        HINTERNET hRequest = WinHttpOpenRequest(
            hConnect, L"POST", L"/v1/chat/completions", nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

        bool ok = false;
        std::string response;
        if (hRequest) {
            const wchar_t* headers = L"Content-Type: application/json\r\n";
            if (WinHttpSendRequest(
                    hRequest, headers, static_cast<DWORD>(-1),
                    const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                    static_cast<DWORD>(body.size()), 0) &&
                WinHttpReceiveResponse(hRequest, nullptr)) {

                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
                    std::string chunk(avail, '\0');
                    DWORD read = 0;
                    if (WinHttpReadData(hRequest, chunk.data(), avail, &read) && read > 0) {
                        response.append(chunk.data(), read);
                    } else {
                        break;
                    }
                }
                ok = !response.empty();
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        if (!ok) {
            onReply(L"(Keine Antwort von LM Studio – läuft der lokale Server?)", false);
            return;
        }

        // Suche nach "choices" -> "message" -> "content"
        size_t choicesPos = response.find("\"choices\"");
        std::string content;
        if (choicesPos != std::string::npos && ExtractFirstJsonString(response, "content", content, choicesPos)) {
            onReply(Utf8ToWide(content), true);
        } else {
            onReply(L"(LM Studio Antwort konnte nicht gelesen werden)", false);
        }
    }).detach();
}

} // namespace mumpf
