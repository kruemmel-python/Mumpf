#include "mumpf_state.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace mumpf {

namespace {

std::wstring FormatFileSize(ULONGLONG bytes) {
    std::wostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (bytes < 1024ULL) {
        return std::to_wstring(bytes) + L" Bytes";
    } else if (bytes < 1024ULL * 1024ULL) {
        oss << (static_cast<double>(bytes) / 1024.0) << L" KB (" << bytes << L" Bytes)";
        return oss.str();
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        oss << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << L" MB";
        return oss.str();
    } else {
        oss << (static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)) << L" GB";
        return oss.str();
    }
}

std::wstring FormatFileTime(const FILETIME& ft) {
    FILETIME localFt{};
    SYSTEMTIME st{};
    if (!FileTimeToLocalFileTime(&ft, &localFt) || !FileTimeToSystemTime(&localFt, &st)) {
        return L"-";
    }
    std::wostringstream oss;
    oss << std::setfill(L'0')
        << std::setw(2) << st.wDay << L"."
        << std::setw(2) << st.wMonth << L"."
        << st.wYear << L" "
        << std::setw(2) << st.wHour << L":"
        << std::setw(2) << st.wMinute << L":"
        << std::setw(2) << st.wSecond;
    return oss.str();
}

std::wstring ReadTextPreviewIfTextFile(const std::wstring& path) {
    std::filesystem::path p(path);
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });

    static const wchar_t* kTextExts[] = {
        L".txt", L".md", L".log", L".json", L".xml", L".yaml", L".yml", L".ini", L".cfg", L".toml",
        L".c", L".h", L".cpp", L".hpp", L".cs", L".py", L".js", L".ts", L".html", L".css", L".ps1",
        L".bat", L".cmd", L".sh", L".kr", L".krproject", L".rs", L".go", L".csv", L".sql"
    };
    bool isText = false;
    for (const wchar_t* e : kTextExts) {
        if (ext == e) { isText = true; break; }
    }
    if (!isText) return {};

    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open()) return {};
    std::string buf(2000, '\0');
    ifs.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    buf.resize(static_cast<size_t>(ifs.gcount()));
    if (buf.empty()) return {};

    int needed = MultiByteToWideChar(CP_UTF8, 0, buf.data(), static_cast<int>(buf.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, buf.data(), static_cast<int>(buf.size()), wide.data(), needed);
    return wide;
}

} // namespace

void MumpfBrain::SetAnim(AnimRow row, bool resetFrame) {
    baseAnim_ = row;
    if (!overrideAnim_.has_value()) {
        if (currentAnim_ != row || resetFrame) {
            currentAnim_ = row;
            currentFrame_ = 0;
            frameTimer_ = 0.0f;
        }
    }
}

void MumpfBrain::SetOverrideAnim(std::optional<AnimRow> anim) {
    if (overrideAnim_ == anim) return;
    overrideAnim_ = anim;
    AnimRow target = overrideAnim_.value_or(baseAnim_);
    if (currentAnim_ != target) {
        currentAnim_ = target;
        currentFrame_ = 0;
        frameTimer_ = 0.0f;
    }
}

void MumpfBrain::Say(const std::wstring& text, float durationSec, bool force) {
    if (muted_ && !force) return;
    bubbleText_ = text;
    bubbleRemaining_ = durationSec;
}

void MumpfBrain::HandleEvent(const MumpfEvent& ev) {
    if (ev.type == L"build_started") {
        activeBuildRunning_ = true;
        activeBuildWarnings_ = 0;
        lastError_.reset();
        detailOpen_ = false;
        returnToIdleAfter_ = 0.0f;
        SetAnim(AnimRow::Running);
        if (!ev.target.empty()) {
            Say(L"Build gestartet (" + ev.target + L"). Ich schau mal mit rein.", 3.5f);
        } else {
            Say(L"Build läuft. Mal sehen, was der Compiler sagt.", 3.5f);
        }
        return;
    }

    if (ev.type == L"diagnostic") {
        if (ev.diagnostic.has_value()) {
            const auto& diag = *ev.diagnostic;
            if (diag.severity == L"warning") {
                activeBuildWarnings_ = (ev.warnings > 0) ? ev.warnings : (activeBuildWarnings_ + 1);
                if (activeBuildWarnings_ == 1) {
                    SetAnim(AnimRow::Review);
                } else if (activeBuildWarnings_ == 2) {
                    SetAnim(AnimRow::Waiting);
                }
            } else if (diag.severity == L"error") {
                if (!lastError_.has_value()) {
                    lastError_ = diag;
                }
                SetAnim(AnimRow::Failed);
            }
        }
        return;
    }

    if (ev.type == L"build_finished") {
        activeBuildRunning_ = false;
        const int warnings = (ev.warnings > 0) ? ev.warnings : activeBuildWarnings_;
        if (!ev.success) {
            if (ev.diagnostic.has_value()) {
                lastError_ = ev.diagnostic;
            }
            SetAnim(AnimRow::Failed);
            returnToIdleAfter_ = 12.0f;
            if (warnings >= 2) {
                Say(L"Bei den Warnungen wollt ich noch höflich sein.", 7.0f);
            } else if (warnings == 1) {
                Say(L"Eine Warnung, dann direkt Abbruch. Klick mich an für die Zeile.", 7.0f);
            } else {
                Say(L"Autsch. Der Build ist rausgeflogen. Klick mich an, ich zeig dir wo.", 7.0f);
            }
        } else {
            lastError_.reset();
            detailOpen_ = false;
            SetAnim(warnings == 0 ? AnimRow::Jumping : AnimRow::Waving);
            returnToIdleAfter_ = 5.0f;
            if (warnings == 0) {
                Say(L"Null Warnungen. Jetzt kann ich wieder atmen.", 6.0f);
            } else {
                Say(L"Durchgelaufen — mit " + std::to_wstring(warnings) + L" Warnung(en). Geht doch!", 5.5f);
            }
        }
        return;
    }

    if (ev.type == L"dialog_opened") {
        SetAnim(AnimRow::Review);
        returnToIdleAfter_ = 10.0f;
        pendingDialogTitle_ = ev.target.empty() ? L"Fehlermeldung" : (L"Dialog: " + ev.target);
        pendingDialogText_ = ev.text.empty() ? L"Ein Programm hat einen Dialog geöffnet." : ev.text;
        Say(L"Der klingt sauer. Soll ich gucken, was er will?", 6.5f);
        return;
    }

    if (ev.type == L"context_changed") {
        if (ev.source == L"browser") {
            activeBrowser_ = ev.target;
            activeIde_.clear();
        } else {
            activeIde_ = ev.target;
            activeBrowser_.clear();
        }
        activeWindowTitle_ = ev.text;
        // Beim Fensterwechsel alte Explorer-Datei / alte Markierung zurücksetzen
        pendingFilePath_.clear();
        pendingFileName_.clear();
        pendingSelectionText_.clear();
        return;
    }

    if (ev.type == L"copy_started") {
        SetAnim(AnimRow::Running);
        returnToIdleAfter_ = 0.0f;
        return;
    }

    if (ev.type == L"copy_estimate_jumped") {
        SetAnim(AnimRow::Waiting);
        returnToIdleAfter_ = 10.0f;
        Say(L"Weck mich, wenn die Schätzung erwachsen ist.", 7.0f);
        return;
    }

    if (ev.type == L"copy_finished") {
        SetAnim(AnimRow::Waving);
        returnToIdleAfter_ = 4.0f;
        Say(L"Fertig kopiert! Alles heil angekommen.", 4.5f);
        return;
    }

    if (ev.type == L"text_selected") {
        pendingSelectionText_ = ev.text;
        pendingSelectionSource_ = ev.target;
        if (!activeBuildRunning_ && !detailOpen_) {
            SetAnim(AnimRow::Review);
            returnToIdleAfter_ = 10.0f;
            Say(L"Text erkannt! Klick mich an und sag mir, was ich damit machen soll.", 7.0f);
        }
        return;
    }

    if (ev.type == L"file_selected") {
        pendingFileName_ = ev.target;
        pendingFilePath_ = ev.text;
        if (!activeBuildRunning_ && !detailOpen_) {
            SetAnim(AnimRow::Review);
            returnToIdleAfter_ = 10.0f;
            Say(L"Soll ich dir Infos zu \"" + ev.target + L"\" ausgeben oder etwas damit machen? Klick mich an.", 7.5f);
        }
        return;
    }

    if (ev.type == L"llm_reply") {
        ApplyLlmReply(ev.text);
        return;
    }

    if (ev.type == L"idle_banter") {
        if (!ev.text.empty() && !muted_) {
            SetAnim(ev.target == L"tip" ? AnimRow::Review : AnimRow::Waving);
            returnToIdleAfter_ = 10.0f;
            Say(ev.text, 10.0f, false);
        }
        return;
    }

    if (ev.type == L"say") {
        if (!ev.text.empty()) {
            Say(ev.text, 5.5f, true);
        }
        return;
    }
}

std::optional<LlmRequest> MumpfBrain::OnClicked() {
    if (detailOpen_) {
        detailOpen_ = false;
        return std::nullopt;
    }

    // 1. Priorität: Markierter Text zur Analyse
    if (!pendingSelectionText_.empty()) {
        detailTitle_ = pendingSelectionSource_.empty()
            ? L"Analyse (markierter Text)"
            : (L"Analyse (" + pendingSelectionSource_ + L")");
        std::wstring snippet = pendingSelectionText_;
        if (snippet.size() > 160) snippet = snippet.substr(0, 157) + L"...";
        detailBody_ = L"Mumpf fragt LM Studio...\n\n\"" + snippet + L"\"";
        detailOpen_ = true;
        SetAnim(AnimRow::Review, false);

        LlmRequest req{ detailTitle_, pendingSelectionText_ };
        pendingSelectionText_.clear();
        return req;
    }

    // 2. Priorität: Letzter Compilerfehler
    if (lastError_.has_value()) {
        const auto& err = *lastError_;
        detailTitle_ = L"Erster relevanter Compilerfehler";
        std::wostringstream oss;
        if (!err.file.empty()) {
            oss << err.file;
            if (err.line > 0) oss << L":" << err.line;
            oss << L"\n";
        }
        oss << (err.message.empty() ? L"(Keine Detailmeldung übermittelt)" : err.message);
        const std::wstring raw = oss.str();
        detailBody_ = raw + L"\n\nMumpf fragt Phi-3 nach einem Tipp...";
        detailOpen_ = true;
        SetAnim(AnimRow::Review, false);
        return LlmRequest{ detailTitle_, raw };
    }

    // 3. Priorität: Letzter Windows-Fehlerdialog
    if (!pendingDialogText_.empty()) {
        detailTitle_ = pendingDialogTitle_.empty() ? L"Fehlermeldung" : pendingDialogTitle_;
        detailBody_ = pendingDialogText_ + L"\n\nMumpf fasst die Meldung mit Phi-3 zusammen...";
        detailOpen_ = true;
        SetAnim(AnimRow::Review, false);
        LlmRequest req{ detailTitle_, pendingDialogText_ };
        pendingDialogText_.clear();
        return req;
    }

    if (activeBuildRunning_) {
        Say(L"Ich beobachte gerade den laufenden Build.", 3.5f, true);
    } else if (!activeIde_.empty()) {
        SetAnim(AnimRow::Waving);
        returnToIdleAfter_ = 2.5f;
        Say(L"Ich schau dir gerade in " + activeIde_ + L" über die Schulter.", 4.0f, true);
    } else {
        SetAnim(AnimRow::Waving);
        returnToIdleAfter_ = 2.5f;
        Say(L"Alles ruhig hier. Markier einfach Text, wenn ich gucken soll.", 4.0f, true);
    }
    return std::nullopt;
}

std::optional<LlmRequest> MumpfBrain::StartSelectionAnalysis(const std::wstring& instruction) {
    if (pendingSelectionText_.empty()) return std::nullopt;
    detailTitle_ = pendingSelectionSource_.empty()
        ? L"Mumpf-Analyse"
        : (L"Mumpf-Analyse (" + pendingSelectionSource_ + L")");
    std::wstring snippet = pendingSelectionText_;
    if (snippet.size() > 140) snippet = snippet.substr(0, 137) + L"...";
    detailBody_ = L"Mumpf fragt LM Studio...\n\n\"" + snippet + L"\"";
    detailOpen_ = true;
    SetAnim(AnimRow::Review, false);

    LlmRequest req{ detailTitle_, pendingSelectionText_, instruction };
    pendingSelectionText_.clear();
    return req;
}

std::optional<LlmRequest> MumpfBrain::StartPageAnalysis(const std::wstring& pageContent, const std::wstring& instruction) {
    detailTitle_ = activeBrowser_.empty() ? L"Webseiten-Analyse" : (activeBrowser_ + L" – Mumpf");
    detailBody_ = L"Mumpf liest die Seite und fragt LM Studio...\n\n" + activeWindowTitle_;
    detailOpen_ = true;
    SetAnim(AnimRow::Review, false);

    std::wstring body = L"Seitentitel: " + activeWindowTitle_;
    if (!pageContent.empty()) {
        body += L"\n\nInhalt:\n" + pageContent;
    }
    return LlmRequest{ detailTitle_, std::move(body), instruction };
}

void MumpfBrain::OpenFileProperties() {
    if (pendingFilePath_.empty()) return;

    const std::wstring path = pendingFilePath_;
    const std::wstring name = pendingFileName_;

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    std::wostringstream oss;
    oss << L"Name: " << name << L"\n";
    oss << L"Pfad: " << path << L"\n";

    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        const bool isDir = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        std::filesystem::path p(path);
        oss << L"Typ: " << (isDir ? L"Ordner" : (p.has_extension() ? p.extension().wstring() : L"Datei")) << L"\n";
        if (!isDir) {
            ULARGE_INTEGER sz{};
            sz.HighPart = fad.nFileSizeHigh;
            sz.LowPart = fad.nFileSizeLow;
            oss << L"Größe: " << FormatFileSize(sz.QuadPart) << L"\n";
        }
        oss << L"Erstellt: " << FormatFileTime(fad.ftCreationTime) << L"\n";
        oss << L"Geändert: " << FormatFileTime(fad.ftLastWriteTime) << L"\n";

        std::wstring attrs;
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) attrs += L"Schreibgeschützt ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) attrs += L"Versteckt ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) attrs += L"System ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) attrs += L"Archiv ";
        oss << L"Attribute: " << (attrs.empty() ? L"Normal" : attrs);
    } else {
        oss << L"(Eigenschaften konnten nicht gelesen werden)";
    }

    activeFilePath_ = path;
    activeFileProps_ = oss.str();
    detailTitle_ = L"Datei-Eigenschaften: " + name;
    detailBody_ = activeFileProps_;
    detailOpen_ = true;
    SetAnim(AnimRow::Review, false);
    Say(L"Hier sind die Eigenschaften. Soll ich sie mit LM Studio analysieren?", 7.0f, true);
}

std::optional<LlmRequest> MumpfBrain::StartFileLlmAnalysis() {
    const std::wstring path = !activeFilePath_.empty() ? activeFilePath_ : pendingFilePath_;
    if (path.empty()) return std::nullopt;

    if (activeFileProps_.empty()) {
        OpenFileProperties();
    }

    std::wstring payload = activeFileProps_;
    const std::wstring preview = ReadTextPreviewIfTextFile(path);
    if (!preview.empty()) {
        payload += L"\n\nInhaltsvorschau:\n" + preview;
    }

    detailBody_ = L"Mumpf analysiert die Datei mit LM Studio...\n\n" + activeFileProps_;
    detailOpen_ = true;
    SetAnim(AnimRow::Review, false);
    activeFilePath_.clear();
    pendingFilePath_.clear();
    pendingFileName_.clear();

    return LlmRequest{
        detailTitle_,
        std::move(payload),
        L"Analysiere diese Datei anhand ihrer Eigenschaften (und falls vorhanden der Inhaltsvorschau). Erklaere kurz auf Deutsch, was das fuer eine Datei ist, wofuer sie genutzt wird und ob etwas auffaellig ist."
    };
}

void MumpfBrain::ApplyLlmReply(const std::wstring& reply) {
    if (!detailOpen_) return;
    detailBody_ = reply;
    SetAnim(AnimRow::Waiting, false);
}

void MumpfBrain::DismissDetail() {
    detailOpen_ = false;
    activeFilePath_.clear();
    activeFileProps_.clear();
}

void MumpfBrain::Update(float dtSeconds) {
    if (bubbleRemaining_ > 0.0f) {
        bubbleRemaining_ -= dtSeconds;
        if (bubbleRemaining_ <= 0.0f) {
            bubbleRemaining_ = 0.0f;
            bubbleText_.clear();
        }
    }

    if (returnToIdleAfter_ > 0.0f) {
        returnToIdleAfter_ -= dtSeconds;
        if (returnToIdleAfter_ <= 0.0f) {
            returnToIdleAfter_ = 0.0f;
            if (!activeBuildRunning_ && !detailOpen_) {
                SetAnim(AnimRow::Idle);
            }
        }
    }

    const AnimSpec spec = SpriteSheet::GetAnimSpec(currentAnim_);
    if (spec.frameCount <= 1 || spec.fps <= 0.0f) return;

    frameTimer_ += dtSeconds;
    const float frameDuration = 1.0f / spec.fps;
    while (frameTimer_ >= frameDuration) {
        frameTimer_ -= frameDuration;
        if (currentFrame_ + 1 < spec.frameCount) {
            ++currentFrame_;
        } else if (spec.loop) {
            currentFrame_ = 0;
        } else {
            // Am letzten Frame stehen bleiben (z.B. failed / jumping)
            currentFrame_ = spec.frameCount - 1;
        }
    }
}

} // namespace mumpf
