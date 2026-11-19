#include "uia_observer.h"

#include <UIAutomation.h>
#include <exdisp.h>
#include <objbase.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>

using Microsoft::WRL::ComPtr;

namespace mumpf {

namespace {

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

bool ContainsAny(const std::wstring& hayLower, std::initializer_list<const wchar_t*> needles) {
    for (const wchar_t* n : needles) {
        if (hayLower.find(n) != std::wstring::npos) return true;
    }
    return false;
}

std::wstring GetWindowTitle(HWND hwnd) {
    wchar_t buf[512]{};
    GetWindowTextW(hwnd, buf, 512);
    return std::wstring(buf);
}

std::wstring GetWindowClass(HWND hwnd) {
    wchar_t buf[128]{};
    GetClassNameW(hwnd, buf, 128);
    return std::wstring(buf);
}

std::wstring GetExplorerSelectedFilePath(HWND explorerHwnd) {
    if (!explorerHwnd) return {};

    ComPtr<IShellWindows> shellWindows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&shellWindows))) || !shellWindows) {
        return {};
    }

    long count = 0;
    if (FAILED(shellWindows->get_Count(&count))) return {};

    std::wstring fallbackResult;

    for (long i = 0; i < count; ++i) {
        VARIANT vi{};
        vi.vt = VT_I4;
        vi.lVal = i;
        ComPtr<IDispatch> disp;
        if (FAILED(shellWindows->Item(vi, &disp)) || !disp) continue;

        ComPtr<IWebBrowserApp> wb;
        if (FAILED(disp.As(&wb)) || !wb) continue;

        SHANDLE_PTR hwndVal = 0;
        if (FAILED(wb->get_HWND(&hwndVal))) continue;
        if (reinterpret_cast<HWND>(hwndVal) != explorerHwnd) continue;

        ComPtr<IServiceProvider> sp;
        if (FAILED(wb.As(&sp)) || !sp) continue;

        ComPtr<IShellBrowser> sb;
        if (FAILED(sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&sb))) || !sb) continue;

        ComPtr<IShellView> sv;
        if (FAILED(sb->QueryActiveShellView(&sv)) || !sv) continue;

        // Unter Windows 11 mit Explorer-Tabs ist das SHELLDLL_DefView-Fenster (sv->GetWindow)
        // NUR im aktuell aktiven Tab sichtbar (IsWindowVisible == TRUE)!
        HWND viewHwnd = nullptr;
        bool isActiveTab = false;
        if (SUCCEEDED(sv->GetWindow(&viewHwnd)) && viewHwnd) {
            isActiveTab = (IsWindowVisible(viewHwnd) != FALSE);
        }

        ComPtr<IDataObject> dataObj;
        if (FAILED(sv->GetItemObject(SVGIO_SELECTION, IID_PPV_ARGS(&dataObj))) || !dataObj) continue;

        FORMATETC fmt{};
        fmt.cfFormat = CF_HDROP;
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex = -1;
        fmt.tymed = TYMED_HGLOBAL;

        STGMEDIUM stg{};
        if (SUCCEEDED(dataObj->GetData(&fmt, &stg))) {
            HDROP hDrop = static_cast<HDROP>(GlobalLock(stg.hGlobal));
            std::wstring result;
            if (hDrop) {
                UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
                if (fileCount >= 1) {
                    wchar_t path[MAX_PATH * 2]{};
                    if (DragQueryFileW(hDrop, 0, path, static_cast<UINT>(std::size(path))) > 0) {
                        result = path;
                    }
                }
                GlobalUnlock(stg.hGlobal);
            }
            ReleaseStgMedium(&stg);
            if (!result.empty()) {
                if (isActiveTab) return result;
                if (fallbackResult.empty()) fallbackResult = std::move(result);
            }
        }
    }
    return fallbackResult;
}

std::wstring TrimWhitespace(std::wstring result) {
    while (!result.empty() && std::iswspace(result.front())) result.erase(result.begin());
    while (!result.empty() && std::iswspace(result.back())) result.pop_back();
    return result;
}

std::wstring ReadClipboardText() {
    if (!OpenClipboard(nullptr)) return {};
    std::wstring text;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const wchar_t* ptr = static_cast<const wchar_t*>(GlobalLock(hData));
        if (ptr) {
            text = ptr;
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return text;
}

void WriteClipboardText(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    if (!text.empty()) {
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* dst = GlobalLock(hMem);
            if (dst) {
                memcpy(dst, text.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            } else {
                GlobalFree(hMem);
            }
        }
    }
    CloseClipboard();
}

// Zerstörungsfreies Auslesen der Textauswahl für Apps ohne UIA TextPattern (z.B. Electron/Chromium/VS Code/Antigravity)
std::wstring CaptureSelectionViaClipboard() {
    const DWORD seqBefore = GetClipboardSequenceNumber();
    const std::wstring savedClip = ReadClipboardText();

    // Kurzes Ctrl+C senden
    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'C';

    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'C';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(4, inputs, sizeof(INPUT));

    // Bis zu 120 ms warten, bis die Ziel-App das Clipboard aktualisiert hat
    bool changed = false;
    for (int i = 0; i < 6; ++i) {
        Sleep(20);
        if (GetClipboardSequenceNumber() != seqBefore) {
            changed = true;
            break;
        }
    }

    if (!changed) return {};

    std::wstring captured = TrimWhitespace(ReadClipboardText());
    // Vorherigen Clipboard-Inhalt sofort wiederherstellen
    WriteClipboardText(savedClip);
    return captured;
}

std::wstring ReadSelectedTextUia(IUIAutomation* uia) {
    if (!uia) return {};
    ComPtr<IUIAutomationElement> focused;
    if (FAILED(uia->GetFocusedElement(&focused)) || !focused) return {};

    ComPtr<IUIAutomationTextPattern> textPattern;
    if (FAILED(focused->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&textPattern))) || !textPattern) {
        return {};
    }

    ComPtr<IUIAutomationTextRangeArray> ranges;
    if (FAILED(textPattern->GetSelection(&ranges)) || !ranges) return {};

    int len = 0;
    if (FAILED(ranges->get_Length(&len)) || len <= 0) return {};

    ComPtr<IUIAutomationTextRange> range;
    if (FAILED(ranges->GetElement(0, &range)) || !range) return {};

    BSTR text = nullptr;
    if (FAILED(range->GetText(600, &text)) || !text) return {};

    std::wstring result(text);
    SysFreeString(text);
    return TrimWhitespace(std::move(result));
}

} // namespace

UiaObserver::~UiaObserver() {
    Stop();
}

bool UiaObserver::Start(EventCallback onEvent) {
    if (running_.load()) return true;
    onEvent_ = std::move(onEvent);
    running_.store(true);
    worker_ = std::thread(&UiaObserver::RunLoop, this);
    return true;
}

void UiaObserver::Stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::wstring UiaObserver::GetProcessName(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return {};

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return {};

    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    std::wstring name;
    if (QueryFullProcessImageNameW(hProc, 0, path, &size)) {
        std::wstring full(path);
        size_t slash = full.find_last_of(L"\\/");
        name = (slash != std::wstring::npos) ? full.substr(slash + 1) : full;
    }
    CloseHandle(hProc);
    return name;
}

std::wstring UiaObserver::DetectIdeName(const std::wstring& procName, const std::wstring& title) {
    const std::wstring p = ToLower(procName);
    const std::wstring t = ToLower(title);

    if (ContainsAny(t, { L"kr compiler", L"kr ide", L"kr_forge" }) || ContainsAny(p, { L"kr-ide", L"kr_compiler" })) {
        return L"KR Compiler IDE";
    }
    if (ContainsAny(t, { L"antigravity" }) || ContainsAny(p, { L"antigravity" })) {
        return L"Antigravity IDE";
    }
    if (ContainsAny(t, { L"codex" }) || ContainsAny(p, { L"codex" })) {
        return L"Codex";
    }
    if (p == L"code.exe" || p == L"code - insiders.exe" || ContainsAny(t, { L"visual studio code" })) {
        return L"VS Code";
    }
    if (p == L"devenv.exe" || ContainsAny(t, { L"microsoft visual studio" })) {
        return L"Visual Studio";
    }
    return {};
}

std::wstring DetectBrowserName(const std::wstring& procName) {
    const std::wstring p = ToLower(procName);
    if (p == L"chrome.exe") return L"Google Chrome";
    if (p == L"msedge.exe") return L"Microsoft Edge";
    if (p == L"firefox.exe") return L"Firefox";
    if (p == L"brave.exe") return L"Brave";
    if (p == L"opera.exe") return L"Opera";
    if (p == L"vivaldi.exe") return L"Vivaldi";
    return {};
}

std::wstring UiaObserver::ReadDialogTextUia(HWND hwnd, void* uiaPtr) {
    auto* uia = static_cast<IUIAutomation*>(uiaPtr);
    if (!uia || !hwnd) return {};

    ComPtr<IUIAutomationElement> root;
    if (FAILED(uia->ElementFromHandle(hwnd, &root)) || !root) return {};

    ComPtr<IUIAutomationCondition> cond;
    VARIANT varType{};
    varType.vt = VT_I4;
    varType.lVal = UIA_TextControlTypeId;
    if (FAILED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, varType, &cond)) || !cond) return {};

    ComPtr<IUIAutomationElementArray> texts;
    if (FAILED(root->FindAll(TreeScope_Descendants, cond.Get(), &texts)) || !texts) return {};

    int count = 0;
    texts->get_Length(&count);
    std::wstring combined;
    const int limit = std::min(count, 8);
    for (int i = 0; i < limit; ++i) {
        ComPtr<IUIAutomationElement> el;
        if (SUCCEEDED(texts->GetElement(i, &el)) && el) {
            BSTR name = nullptr;
            if (SUCCEEDED(el->get_CurrentName(&name)) && name) {
                std::wstring piece(name);
                SysFreeString(name);
                if (piece.size() > 3) {
                    if (!combined.empty()) combined += L"\n";
                    combined += piece;
                }
            }
        }
    }
    return combined;
}

void UiaObserver::RunLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ComPtr<IUIAutomation> uia;
    CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));

    HWND lastDialogHwnd = nullptr;
    HWND lastCopyHwnd = nullptr;
    bool copyWasMinutes = false;
    bool copyJumpReported = false;
    std::wstring lastContextKey;
    std::wstring lastExplorerFile;

    // Maus-Drag / Markierungs-Tracking
    bool lbtnDown = false;
    POINT downPt{};
    ULONGLONG lastUiaPollMs = 0;
    const DWORD ownPid = GetCurrentProcessId();

    while (running_.load()) {
        const ULONGLONG nowMs = GetTickCount64();

        // Schnelle Erkennung von Maus-Markierungen (Drag > 12 px)
        const bool curDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (curDown && !lbtnDown) {
            lbtnDown = true;
            GetCursorPos(&downPt);
        } else if (!curDown && lbtnDown) {
            lbtnDown = false;
            POINT upPt{};
            GetCursorPos(&upPt);
            const int dx = std::abs(upPt.x - downPt.x);
            const int dy = std::abs(upPt.y - downPt.y);

            if (dx > 12 || dy > 12) {
                HWND fg = GetForegroundWindow();
                DWORD fgPid = 0;
                if (fg) GetWindowThreadProcessId(fg, &fgPid);
                const bool modsPressed =
                    ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) ||
                    ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0);

                if (fg && fgPid != ownPid && !modsPressed) {
                    Sleep(60); // Ziel-App kurz Zeit geben, die Markierung abzuschließen
                    std::wstring selected = ReadSelectedTextUia(uia.Get());
                    if (selected.size() < 4) {
                        selected = CaptureSelectionViaClipboard();
                    }
                    if (selected.size() >= 4) {
                        const std::wstring title = GetWindowTitle(fg);
                        const std::wstring proc = GetProcessName(fg);
                        const std::wstring ide = DetectIdeName(proc, title);
                        if (onEvent_) {
                            MumpfEvent ev;
                            ev.source = L"selection";
                            ev.type = L"text_selected";
                            ev.target = ide.empty() ? (title.empty() ? proc : title) : ide;
                            ev.text = selected;
                            onEvent_(std::move(ev));
                        }
                    }
                }
            }
        }

        // UIA-Alltagssensoren alle ~500 ms
        if (nowMs - lastUiaPollMs >= 500) {
            lastUiaPollMs = nowMs;
            HWND fg = GetForegroundWindow();
            DWORD fgPid = 0;
            if (fg) GetWindowThreadProcessId(fg, &fgPid);

            if (fg && fgPid != ownPid && IsWindowVisible(fg)) {
                const std::wstring cls = GetWindowClass(fg);
                const std::wstring title = GetWindowTitle(fg);
                const std::wstring titleLower = ToLower(title);
                const std::wstring proc = GetProcessName(fg);

                // 1. IDE / Browser / App-Kontext verfolgen
                const std::wstring ide = DetectIdeName(proc, title);
                const std::wstring browser = ide.empty() ? DetectBrowserName(proc) : std::wstring{};
                const std::wstring appLabel = !ide.empty() ? ide : (!browser.empty() ? browser : (title.empty() ? proc : title));
                const std::wstring key = appLabel + L"|" + title;
                if (key != lastContextKey) {
                    lastContextKey = key;
                    if (onEvent_) {
                        MumpfEvent ev;
                        ev.source = !browser.empty() ? L"browser" : L"uia";
                        ev.type = L"context_changed";
                        ev.target = !ide.empty() ? ide : browser;
                        ev.text = title;
                        onEvent_(std::move(ev));
                    }
                }

                // 2. Standard-Fehlerdialoge (#32770 oder Fehler-Fenster)
                if (fg != lastDialogHwnd &&
                    (cls == L"#32770" || ContainsAny(titleLower, { L"fehler", L"error", L"warnung", L"warning", L"exception", L"absturz" }))) {
                    const std::wstring body = ReadDialogTextUia(fg, uia.Get());
                    const std::wstring bodyLower = ToLower(body);
                    if (ContainsAny(titleLower, { L"fehler", L"error", L"warnung", L"warning", L"exception", L"nicht gefunden", L"zugriff" }) ||
                        ContainsAny(bodyLower, { L"fehler", L"error", L"fehlgeschlagen", L"failed", L"exception", L"konnte nicht" })) {
                        lastDialogHwnd = fg;
                        if (onEvent_) {
                            MumpfEvent ev;
                            ev.source = L"uia";
                            ev.type = L"dialog_opened";
                            ev.target = title.empty() ? proc : title;
                            ev.text = body.empty() ? title : (title + L"\n" + body);
                            onEvent_(std::move(ev));
                        }
                    }
                }

                // 3. Windows Explorer Kopierdialog (auch im Hintergrund via FindWindowW)
                HWND copyHwnd = FindWindowW(L"OperationStatusWindow", nullptr);
                if (!copyHwnd && (cls == L"OperationStatusWindow" ||
                    (ToLower(proc) == L"explorer.exe" && ContainsAny(titleLower, { L"kopier", L"verschieb", L"copying", L"moving" })))) {
                    copyHwnd = fg;
                }

                if (copyHwnd && IsWindowVisible(copyHwnd)) {
                    if (copyHwnd != lastCopyHwnd) {
                        lastCopyHwnd = copyHwnd;
                        copyWasMinutes = false;
                        copyJumpReported = false;
                        if (onEvent_) {
                            MumpfEvent ev;
                            ev.source = L"uia";
                            ev.type = L"copy_started";
                            ev.text = GetWindowTitle(copyHwnd);
                            onEvent_(std::move(ev));
                        }
                    }
                    const std::wstring statusText = ToLower(ReadDialogTextUia(copyHwnd, uia.Get()));
                    if (ContainsAny(statusText, { L"minute", L"sekunde", L"second" })) {
                        copyWasMinutes = true;
                    }
                    if (copyWasMinutes && !copyJumpReported && ContainsAny(statusText, { L"stunde", L"hour", L"tag", L"day" })) {
                        copyJumpReported = true;
                        if (onEvent_) {
                            MumpfEvent ev;
                            ev.source = L"uia";
                            ev.type = L"copy_estimate_jumped";
                            ev.text = L"Explorer-Restzeit ist von Minuten auf Stunden gesprungen.";
                            onEvent_(std::move(ev));
                        }
                    }
                } else if (lastCopyHwnd && !IsWindow(lastCopyHwnd)) {
                    lastCopyHwnd = nullptr;
                    if (onEvent_) {
                        MumpfEvent ev;
                        ev.source = L"uia";
                        ev.type = L"copy_finished";
                        onEvent_(std::move(ev));
                    }
                }

                // 4. Markierte Datei im Windows Explorer (CabinetWClass)
                if (cls == L"CabinetWClass") {
                    const std::wstring selectedFile = GetExplorerSelectedFilePath(fg);
                    if (!selectedFile.empty() && selectedFile != lastExplorerFile) {
                        lastExplorerFile = selectedFile;
                        size_t slash = selectedFile.find_last_of(L"\\/");
                        std::wstring fileName = (slash != std::wstring::npos) ? selectedFile.substr(slash + 1) : selectedFile;
                        if (onEvent_) {
                            MumpfEvent ev;
                            ev.source = L"explorer";
                            ev.type = L"file_selected";
                            ev.target = fileName;
                            ev.text = selectedFile;
                            onEvent_(std::move(ev));
                        }
                    } else if (selectedFile.empty()) {
                        lastExplorerFile.clear();
                    }
                }
            }
        }

        Sleep(40);
    }

    uia.Reset();
    CoUninitialize();
}

} // namespace mumpf
