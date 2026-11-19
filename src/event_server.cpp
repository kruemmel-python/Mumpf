#include "event_server.h"

#include <cctype>
#include <sstream>
#include <string>

namespace mumpf {

namespace {

constexpr wchar_t kPipeName[] = LR"(\\.\pipe\mumpf-events)";

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

bool ExtractJsonString(const std::string& json, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != '"') return false;
    ++pos;

    std::string result;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '\\' && pos < json.size()) {
            char next = json[pos++];
            switch (next) {
                case '"':  result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case 'n':  result.push_back('\n'); break;
                case 'r':  result.push_back('\r'); break;
                case 't':  result.push_back('\t'); break;
                default:   result.push_back(next); break;
            }
        } else if (c == '"') {
            out = std::move(result);
            return true;
        } else {
            result.push_back(c);
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
    if (pos >= json.size()) return false;

    size_t end = pos;
    if (json[end] == '-') ++end;
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
    if (json.compare(pos, 4, "true") == 0) {
        out = true;
        return true;
    }
    if (json.compare(pos, 5, "false") == 0) {
        out = false;
        return true;
    }
    return false;
}

} // namespace

EventServer::~EventServer() {
    Stop();
}

bool EventServer::Start(Callback onEvent) {
    if (running_.load()) return true;
    onEvent_ = std::move(onEvent);
    running_.store(true);
    worker_ = std::thread(&EventServer::RunLoop, this);
    return true;
}

void EventServer::Stop() {
    if (!running_.exchange(false)) return;

    // Kurz mit der Pipe verbinden, damit ConnectNamedPipe sauber aufwacht.
    HANDLE hWake = CreateFileW(
        kPipeName,
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (hWake != INVALID_HANDLE_VALUE) {
        CloseHandle(hWake);
    }

    if (worker_.joinable()) {
        worker_.join();
    }
}

MumpfEvent EventServer::ParseJsonLine(const std::string& utf8Line) {
    MumpfEvent ev;
    std::string tmp;
    if (ExtractJsonString(utf8Line, "source", tmp)) ev.source = Utf8ToWide(tmp);
    if (ExtractJsonString(utf8Line, "type", tmp)) ev.type = Utf8ToWide(tmp);
    if (ExtractJsonString(utf8Line, "target", tmp)) ev.target = Utf8ToWide(tmp);
    if (ExtractJsonString(utf8Line, "text", tmp)) ev.text = Utf8ToWide(tmp);

    ExtractJsonBool(utf8Line, "success", ev.success);
    ExtractJsonInt(utf8Line, "warnings", ev.warnings);
    ExtractJsonInt(utf8Line, "errors", ev.errors);

    DiagnosticInfo diag;
    bool hasDiag = false;
    if (ExtractJsonString(utf8Line, "severity", tmp)) {
        diag.severity = Utf8ToWide(tmp);
        hasDiag = true;
    }
    if (ExtractJsonString(utf8Line, "file", tmp)) {
        diag.file = Utf8ToWide(tmp);
        hasDiag = true;
    }
    if (ExtractJsonInt(utf8Line, "line", diag.line)) {
        hasDiag = true;
    }
    if (ExtractJsonString(utf8Line, "message", tmp)) {
        diag.message = Utf8ToWide(tmp);
        hasDiag = true;
    }
    if (hasDiag) {
        ev.diagnostic = std::move(diag);
    }

    return ev;
}

void EventServer::RunLoop() {
    while (running_.load()) {
        HANDLE hPipe = CreateNamedPipeW(
            kPipeName,
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096,
            16384,
            0,
            nullptr);

        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(250);
            continue;
        }

        BOOL connected = ConnectNamedPipe(hPipe, nullptr)
            ? TRUE
            : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (connected && running_.load()) {
            std::string buffer;
            char chunk[1024];
            DWORD bytesRead = 0;
            while (ReadFile(hPipe, chunk, sizeof(chunk), &bytesRead, nullptr) && bytesRead > 0) {
                buffer.append(chunk, bytesRead);
            }

            std::istringstream iss(buffer);
            std::string line;
            while (std::getline(iss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                MumpfEvent ev = ParseJsonLine(line);
                if (!ev.type.empty() && onEvent_) {
                    onEvent_(std::move(ev));
                }
            }
        }

        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
}

} // namespace mumpf
