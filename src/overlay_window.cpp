#include "overlay_window.h"

#include <dwmapi.h>
#include <shobjidl.h>
#include <UIAutomation.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace mumpf {

namespace {

constexpr wchar_t kWindowClassName[] = L"MumpfWindowsOverlayClass";
constexpr UINT_PTR kAnimTimerId = 1;
constexpr UINT kMsgEventReady = WM_APP + 1;
constexpr UINT kMsgTrayIcon = WM_APP + 2;

constexpr UINT kCmdDemo = 1001;
constexpr UINT kCmdAutoDock = 1002;
constexpr UINT kCmdMute = 1003;
constexpr UINT kCmdExit = 1004;
constexpr UINT kCmdIdleBanter = 1005;

constexpr UINT kCmdSelExplain = 2001;
constexpr UINT kCmdSelSummarize = 2002;
constexpr UINT kCmdSelDebug = 2003;
constexpr UINT kCmdSelTranslate = 2004;
constexpr UINT kCmdSelCustom = 2005;
constexpr UINT kCmdSelDismiss = 2006;

constexpr UINT kCmdAnswerCopy = 3001;
constexpr UINT kCmdAnswerClose = 3002;
constexpr UINT kCmdAnswerAnalyze = 3003;

constexpr UINT kCmdBrowserSummarize = 4001;
constexpr UINT kCmdBrowserBullets = 4002;
constexpr UINT kCmdBrowserTranslate = 4003;
constexpr UINT kCmdBrowserAsk = 4004;
constexpr UINT kCmdBrowserCopyTitle = 4005;

constexpr UINT kCmdFileProps = 5001;
constexpr UINT kCmdFileAnalyze = 5002;
constexpr UINT kCmdFileOpen = 5003;
constexpr UINT kCmdFileCopy = 5004;
constexpr UINT kCmdFileMove = 5005;
constexpr UINT kCmdFileDelete = 5006;
constexpr UINT kCmdFileDismiss = 5007;

// Sprite sitzt unten rechts im Canvas, damit links/oben Platz für Sprechblase & Detailkarte ist.
constexpr int kSpriteX = OverlayWindow::kCanvasWidth - OverlayWindow::kSpriteDrawW - 12;
constexpr int kSpriteY = OverlayWindow::kCanvasHeight - OverlayWindow::kSpriteDrawH - 8;

constexpr float kDetailX = 12.0f;
constexpr float kDetailY = 12.0f;
constexpr float kDetailW = 396.0f;
constexpr float kDetailH = 205.0f;

} // namespace

OverlayWindow* OverlayWindow::s_instance_ = nullptr;

OverlayWindow::~OverlayWindow() {
    CloseAnswerWindow();
    if (answerFont_) { DeleteObject(answerFont_); answerFont_ = nullptr; }
    if (answerBgBrush_) { DeleteObject(answerBgBrush_); answerBgBrush_ = nullptr; }
    RemoveTrayIcon();
    if (winEventHook_) {
        UnhookWinEvent(winEventHook_);
        winEventHook_ = nullptr;
    }
    if (memDC_) {
        if (oldBitmap_) SelectObject(memDC_, oldBitmap_);
        if (dibBitmap_) DeleteObject(dibBitmap_);
        DeleteDC(memDC_);
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (s_instance_ == this) {
        s_instance_ = nullptr;
    }
}

bool OverlayWindow::Create(HINSTANCE hInstance, const std::wstring& spritesheetPath) {
    hInstance_ = hInstance;
    s_instance_ = this;

    if (!sheet_.LoadFromFile(spritesheetPath)) {
        MessageBoxW(nullptr, (L"Spritesheet konnte nicht geladen werden:\n" + spritesheetPath).c_str(),
                    L"Mumpf", MB_ICONERROR | MB_OK);
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &OverlayWindow::WndProcStatic;
    wc.hInstance = hInstance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;
    RegisterClassExW(&wc);

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    posX_ = static_cast<float>(workArea.right - kCanvasWidth - 20);
    posY_ = static_cast<float>(workArea.bottom - kCanvasHeight - 10);
    targetX_ = posX_;
    targetY_ = posY_;

    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    hwnd_ = CreateWindowExW(
        exStyle,
        kWindowClassName,
        L"Mumpf",
        WS_POPUP,
        static_cast<int>(posX_),
        static_cast<int>(posY_),
        kCanvasWidth,
        kCanvasHeight,
        nullptr,
        nullptr,
        hInstance_,
        this);

    if (!hwnd_) return false;

    if (!InitGraphics()) {
        MessageBoxW(nullptr, L"Direct2D / DirectWrite Initialisierung fehlgeschlagen.", L"Mumpf", MB_ICONERROR | MB_OK);
        return false;
    }

    InitTrayIcon();

    winEventHook_ = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_OBJECT_LOCATIONCHANGE,
        nullptr,
        &OverlayWindow::WinEventProcStatic,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    UpdateDockTarget(GetForegroundWindow());

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    lastTickMs_ = GetTickCount64();
    SetTimer(hwnd_, kAnimTimerId, 33, nullptr); // ~30 FPS
    RenderFrame();
    return true;
}

bool OverlayWindow::InitGraphics() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = dwriteFactory_->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        15.0f,
        L"de-DE",
        &bubbleFont_);
    if (FAILED(hr)) return false;
    bubbleFont_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    hr = dwriteFactory_->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        14.0f,
        L"de-DE",
        &titleFont_);
    if (FAILED(hr)) return false;

    hr = dwriteFactory_->CreateTextFormat(
        L"Consolas",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        13.0f,
        L"de-DE",
        &bodyFont_);
    if (FAILED(hr)) return false;
    bodyFont_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    // 32bpp Top-Down DIB-Section für UpdateLayeredWindow
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = kCanvasWidth;
    bmi.bmiHeader.biHeight = -kCanvasHeight; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screenDC = GetDC(nullptr);
    memDC_ = CreateCompatibleDC(screenDC);
    dibBitmap_ = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
    ReleaseDC(nullptr, screenDC);

    if (!memDC_ || !dibBitmap_) return false;
    oldBitmap_ = SelectObject(memDC_, dibBitmap_);

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f,
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT);

    hr = d2dFactory_->CreateDCRenderTarget(&props, &renderTarget_);
    if (FAILED(hr)) return false;

    if (!sheet_.CreateD2DBitmap(renderTarget_.Get(), sheetBitmap_)) {
        return false;
    }

    return true;
}

void OverlayWindow::InitTrayIcon() {
    trayIcon_.cbSize = sizeof(trayIcon_);
    trayIcon_.hWnd = hwnd_;
    trayIcon_.uID = 1;
    trayIcon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    trayIcon_.uCallbackMessage = kMsgTrayIcon;
    trayIcon_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(trayIcon_.szTip, L"Mumpf – Windows 11 Helferlein");
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &trayIcon_) != FALSE;
}

void OverlayWindow::RemoveTrayIcon() {
    if (trayAdded_) {
        Shell_NotifyIconW(NIM_DELETE, &trayIcon_);
        trayAdded_ = false;
    }
}

void OverlayWindow::ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, kCmdDemo, L"Demo: KR Build-Zyklus abspielen");
    AppendMenuW(menu, MF_STRING | (autoDock_ ? MF_CHECKED : 0), kCmdAutoDock, L"Am aktiven Fenster andocken");
    AppendMenuW(menu, MF_STRING | (idleBanterEnabled_ ? MF_CHECKED : 0), kCmdIdleBanter, L"Spontane LLM-Sprüche (alle 2 Min.)");
    AppendMenuW(menu, MF_STRING | (brain_.IsMuted() ? MF_CHECKED : 0), kCmdMute, L"Stummschalten");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"Mumpf beenden");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

void OverlayWindow::DispatchLlmRequest(const LlmRequest& req) {
    if (!llmClient_) return;
    llmClient_->AskAsync(req.title, req.body, req.instruction, [this](std::wstring reply, bool) {
        MumpfEvent ev;
        ev.source = L"llm";
        ev.type = L"llm_reply";
        ev.text = std::move(reply);
        EnqueueEvent(std::move(ev));
    });
}

std::wstring OverlayWindow::PromptCustomInstruction() {
    struct PromptState {
        HWND edit = nullptr;
        std::wstring result;
        bool accepted = false;
        bool done = false;
    } state;

    static bool clsRegistered = false;
    constexpr wchar_t kPromptCls[] = L"MumpfPromptDialogClass";
    if (!clsRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = [](HWND h, UINT m, WPARAM wp, LPARAM lp) -> LRESULT {
            auto* st = reinterpret_cast<PromptState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
            if (m == WM_NCCREATE) {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
                return TRUE;
            }
            if (m == WM_COMMAND && st) {
                if (LOWORD(wp) == IDOK) {
                    wchar_t buf[512]{};
                    GetWindowTextW(st->edit, buf, 512);
                    st->result = buf;
                    st->accepted = true;
                    st->done = true;
                    DestroyWindow(h);
                    return 0;
                }
                if (LOWORD(wp) == IDCANCEL) {
                    st->done = true;
                    DestroyWindow(h);
                    return 0;
                }
            }
            if (m == WM_CLOSE && st) {
                st->done = true;
                DestroyWindow(h);
                return 0;
            }
            return DefWindowProcW(h, m, wp, lp);
        };
        wc.hInstance = hInstance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kPromptCls;
        RegisterClassExW(&wc);
        clsRegistered = true;
    }

    POINT pt{};
    GetCursorPos(&pt);
    const int dlgW = 420;
    const int dlgH = 145;
    int x = std::max(40, static_cast<int>(pt.x) - dlgW);
    int y = std::max(40, static_cast<int>(pt.y) - dlgH - 20);

    HWND dlg = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kPromptCls,
        L"Was soll Mumpf mit dem Text machen?",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, dlgW, dlgH,
        hwnd_, nullptr, hInstance_, &state);
    if (!dlg) return {};

    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HWND lbl = CreateWindowExW(0, L"STATIC", L"Anweisung an Mumpf (z.B. \"Kurz erklären\", \"Auf Fehler prüfen\"):",
        WS_CHILD | WS_VISIBLE, 12, 10, dlgW - 36, 20, dlg, nullptr, hInstance_, nullptr);
    state.edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 12, 34, dlgW - 36, 26, dlg, nullptr, hInstance_, nullptr);
    HWND btnOk = CreateWindowExW(0, L"BUTTON", L"Los!",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, dlgW - 190, 72, 75, 28, dlg, reinterpret_cast<HMENU>(IDOK), hInstance_, nullptr);
    HWND btnCancel = CreateWindowExW(0, L"BUTTON", L"Abbrechen",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP, dlgW - 108, 72, 84, 28, dlg, reinterpret_cast<HMENU>(IDCANCEL), hInstance_, nullptr);

    SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(state.edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(btnOk, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(btnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    SetFocus(state.edit);

    MSG msg{};
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && msg.hwnd == state.edit) {
            SendMessageW(dlg, WM_COMMAND, IDOK, 0);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            SendMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return state.accepted ? state.result : std::wstring{};
}

void OverlayWindow::ShowSelectionActionMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, kCmdSelExplain, L"Erklären (kurz & verständlich)");
    AppendMenuW(menu, MF_STRING, kCmdSelSummarize, L"Zusammenfassen");
    AppendMenuW(menu, MF_STRING, kCmdSelDebug, L"Code / Fehler analysieren");
    AppendMenuW(menu, MF_STRING, kCmdSelTranslate, L"Auf Deutsch übersetzen");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdSelCustom, L"Eigene Anweisung eingeben...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdSelDismiss, L"Verwerfen");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    UINT cmd = static_cast<UINT>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);

    std::wstring instruction;
    switch (cmd) {
        case kCmdSelExplain:
            instruction = L"Erklaere diesen Text kurz, klar und verstaendlich auf Deutsch (maximal 3 Saetze).";
            break;
        case kCmdSelSummarize:
            instruction = L"Fasse diesen Text extrem kompakt in 2-3 kurzen Saetzen auf Deutsch zusammen.";
            break;
        case kCmdSelDebug:
            instruction = L"Analysiere diesen Code oder diese Fehlermeldung: Was ist die Ursache und was ist der naechste Schritt?";
            break;
        case kCmdSelTranslate:
            instruction = L"Uebersetze diesen Text natuerlich und praezise ins Deutsche.";
            break;
        case kCmdSelCustom:
            instruction = PromptCustomInstruction();
            if (instruction.empty()) return;
            break;
        case kCmdSelDismiss:
            brain_.ClearPendingSelection();
            return;
        default:
            return;
    }

    auto req = brain_.StartSelectionAnalysis(instruction);
    if (trackedHwnd_ && IsWindow(trackedHwnd_)) {
        SetForegroundWindow(trackedHwnd_);
    }
    if (req.has_value()) {
        ShowOrUpdateAnswerWindow(brain_.DetailTitle(), brain_.DetailBody());
        DispatchLlmRequest(*req);
        RenderFrame();
    }
}

std::wstring OverlayWindow::ReadBrowserPageSnippet(HWND browserHwnd) {
    if (!browserHwnd || !IsWindow(browserHwnd)) return {};

    ComPtr<IUIAutomation> uia;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) || !uia) {
        return {};
    }

    ComPtr<IUIAutomationElement> root;
    if (FAILED(uia->ElementFromHandle(browserHwnd, &root)) || !root) return {};

    // Suche nach dem Dokument-Element (Webseite) im Browser
    ComPtr<IUIAutomationCondition> docCond;
    VARIANT vDoc{};
    vDoc.vt = VT_I4;
    vDoc.lVal = UIA_DocumentControlTypeId;
    if (SUCCEEDED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, vDoc, &docCond)) && docCond) {
        ComPtr<IUIAutomationElement> docEl;
        if (SUCCEEDED(root->FindFirst(TreeScope_Descendants, docCond.Get(), &docEl)) && docEl) {
            ComPtr<IUIAutomationTextPattern> tp;
            if (SUCCEEDED(docEl->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&tp))) && tp) {
                ComPtr<IUIAutomationTextRange> range;
                if (SUCCEEDED(tp->get_DocumentRange(&range)) && range) {
                    BSTR txt = nullptr;
                    if (SUCCEEDED(range->GetText(2400, &txt)) && txt) {
                        std::wstring out(txt);
                        SysFreeString(txt);
                        if (out.size() > 20) return out;
                    }
                }
            }
        }
    }

    // Fallback: sichtbare Text-Elemente im Browser sammeln
    ComPtr<IUIAutomationCondition> textCond;
    VARIANT vText{};
    vText.vt = VT_I4;
    vText.lVal = UIA_TextControlTypeId;
    if (FAILED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, vText, &textCond)) || !textCond) return {};

    ComPtr<IUIAutomationElementArray> items;
    if (FAILED(root->FindAll(TreeScope_Descendants, textCond.Get(), &items)) || !items) return {};

    int count = 0;
    items->get_Length(&count);
    std::wstring combined;
    const int limit = std::min(count, 45);
    for (int i = 0; i < limit && combined.size() < 2200; ++i) {
        ComPtr<IUIAutomationElement> el;
        if (SUCCEEDED(items->GetElement(i, &el)) && el) {
            BSTR name = nullptr;
            if (SUCCEEDED(el->get_CurrentName(&name)) && name) {
                std::wstring piece(name);
                SysFreeString(name);
                if (piece.size() > 8) {
                    if (!combined.empty()) combined += L"\n";
                    combined += piece;
                }
            }
        }
    }
    return combined;
}

void OverlayWindow::ShowBrowserActionMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    const std::wstring header = brain_.ActiveBrowser() + L" – Optionen";
    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, header.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdBrowserSummarize, L"Aktuelle Seite zusammenfassen");
    AppendMenuW(menu, MF_STRING, kCmdBrowserBullets, L"Wichtigste Punkte (Stichpunkte)");
    AppendMenuW(menu, MF_STRING, kCmdBrowserTranslate, L"Seiteninhalt auf Deutsch erklären");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdBrowserAsk, L"Frage zur aktuellen Seite stellen...");
    AppendMenuW(menu, MF_STRING, kCmdBrowserCopyTitle, L"Seitentitel kopieren");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    UINT cmd = static_cast<UINT>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);

    if (cmd == kCmdBrowserCopyTitle) {
        if (OpenClipboard(hwnd_)) {
            EmptyClipboard();
            const auto& t = brain_.ActiveWindowTitle();
            const size_t bytes = (t.size() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hMem) {
                void* dst = GlobalLock(hMem);
                if (dst) {
                    memcpy(dst, t.c_str(), bytes);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                } else {
                    GlobalFree(hMem);
                }
            }
            CloseClipboard();
        }
        if (trackedHwnd_ && IsWindow(trackedHwnd_)) SetForegroundWindow(trackedHwnd_);
        return;
    }

    std::wstring instruction;
    switch (cmd) {
        case kCmdBrowserSummarize:
            instruction = L"Fasse diese Webseite klar und verstaendlich auf Deutsch in 3-5 kurzen Saetzen zusammen.";
            break;
        case kCmdBrowserBullets:
            instruction = L"Extrahiere die wichtigsten Punkte dieser Webseite als kurze, uebersichtliche Stichpunkte auf Deutsch.";
            break;
        case kCmdBrowserTranslate:
            instruction = L"Erklaere den Inhalt dieser Webseite verstaendlich auf Deutsch.";
            break;
        case kCmdBrowserAsk:
            instruction = PromptCustomInstruction();
            if (instruction.empty()) {
                if (trackedHwnd_ && IsWindow(trackedHwnd_)) SetForegroundWindow(trackedHwnd_);
                return;
            }
            break;
        default:
            if (trackedHwnd_ && IsWindow(trackedHwnd_)) SetForegroundWindow(trackedHwnd_);
            return;
    }

    const std::wstring pageSnippet = ReadBrowserPageSnippet(trackedHwnd_);
    auto req = brain_.StartPageAnalysis(pageSnippet, instruction);
    if (trackedHwnd_ && IsWindow(trackedHwnd_)) {
        SetForegroundWindow(trackedHwnd_);
    }
    if (req.has_value()) {
        ShowOrUpdateAnswerWindow(brain_.DetailTitle(), brain_.DetailBody());
        DispatchLlmRequest(*req);
        RenderFrame();
    }
}

std::wstring OverlayWindow::PickTargetFolder(const std::wstring& title) {
    ComPtr<IFileDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) || !dlg) {
        return {};
    }
    DWORD opts = 0;
    if (SUCCEEDED(dlg->GetOptions(&opts))) {
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    }
    dlg->SetTitle(title.c_str());

    if (FAILED(dlg->Show(hwnd_))) return {};

    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item)) || !item) return {};

    PWSTR pszPath = nullptr;
    std::wstring folder;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath) {
        folder = pszPath;
        CoTaskMemFree(pszPath);
    }
    return folder;
}

void OverlayWindow::ShowExplorerFileActionMenu() {
    if (!brain_.HasPendingFile()) return;

    const std::wstring filePath = brain_.PendingFilePath();
    const std::wstring fileName = brain_.PendingFileName();

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    const std::wstring header = L"Datei: " + fileName;
    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, header.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdFileProps, L"Eigenschaften anzeigen (+ LLM-Analyse)");
    AppendMenuW(menu, MF_STRING, kCmdFileAnalyze, L"Direkt mit LM Studio analysieren");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdFileOpen, L"Öffnen");
    AppendMenuW(menu, MF_STRING, kCmdFileCopy, L"Kopieren nach...");
    AppendMenuW(menu, MF_STRING, kCmdFileMove, L"Verschieben nach...");
    AppendMenuW(menu, MF_STRING, kCmdFileDelete, L"Löschen (Papierkorb)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdFileDismiss, L"Verwerfen");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    UINT cmd = static_cast<UINT>(TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd_, nullptr));
    DestroyMenu(menu);

    auto makeDoubleNull = [](const std::wstring& s) {
        std::wstring out = s;
        out.push_back(L'\0');
        out.push_back(L'\0');
        return out;
    };

    switch (cmd) {
        case kCmdFileProps:
            brain_.OpenFileProperties();
            ShowOrUpdateAnswerWindow(brain_.DetailTitle(), brain_.DetailBody());
            RenderFrame();
            break;

        case kCmdFileAnalyze: {
            auto req = brain_.StartFileLlmAnalysis();
            if (req.has_value()) {
                ShowOrUpdateAnswerWindow(brain_.DetailTitle(), brain_.DetailBody());
                DispatchLlmRequest(*req);
                RenderFrame();
            }
            break;
        }

        case kCmdFileOpen:
            ShellExecuteW(nullptr, L"open", filePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            brain_.ClearPendingFile();
            break;

        case kCmdFileCopy: {
            const std::wstring targetDir = PickTargetFolder(L"Zielordner zum Kopieren wählen");
            if (!targetDir.empty()) {
                std::wstring from = makeDoubleNull(filePath);
                std::wstring to = makeDoubleNull(targetDir);
                SHFILEOPSTRUCTW op{};
                op.hwnd = hwnd_;
                op.wFunc = FO_COPY;
                op.pFrom = from.c_str();
                op.pTo = to.c_str();
                op.fFlags = FOF_ALLOWUNDO;
                if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted) {
                    MumpfEvent ev;
                    ev.type = L"say";
                    ev.text = L"Kopiert nach " + targetDir + L"!";
                    brain_.HandleEvent(ev);
                }
            }
            brain_.ClearPendingFile();
            break;
        }

        case kCmdFileMove: {
            const std::wstring targetDir = PickTargetFolder(L"Zielordner zum Verschieben wählen");
            if (!targetDir.empty()) {
                std::wstring from = makeDoubleNull(filePath);
                std::wstring to = makeDoubleNull(targetDir);
                SHFILEOPSTRUCTW op{};
                op.hwnd = hwnd_;
                op.wFunc = FO_MOVE;
                op.pFrom = from.c_str();
                op.pTo = to.c_str();
                op.fFlags = FOF_ALLOWUNDO;
                if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted) {
                    MumpfEvent ev;
                    ev.type = L"say";
                    ev.text = L"Verschoben nach " + targetDir + L"!";
                    brain_.HandleEvent(ev);
                }
            }
            brain_.ClearPendingFile();
            break;
        }

        case kCmdFileDelete: {
            std::wstring from = makeDoubleNull(filePath);
            SHFILEOPSTRUCTW op{};
            op.hwnd = hwnd_;
            op.wFunc = FO_DELETE;
            op.pFrom = from.c_str();
            op.fFlags = FOF_ALLOWUNDO; // Sicher in den Papierkorb (mit Windows-Bestätigung)
            if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted) {
                MumpfEvent ev;
                ev.type = L"say";
                ev.text = L"Ab in den Papierkorb damit!";
                brain_.HandleEvent(ev);
            }
            brain_.ClearPendingFile();
            break;
        }

        case kCmdFileDismiss:
            brain_.ClearPendingFile();
            break;

        default:
            break;
    }

    if (trackedHwnd_ && IsWindow(trackedHwnd_)) {
        SetForegroundWindow(trackedHwnd_);
    }
    RenderFrame();
}

void OverlayWindow::CloseAnswerWindow() {
    if (answerHwnd_) {
        DestroyWindow(answerHwnd_);
        answerHwnd_ = nullptr;
        answerEditHwnd_ = nullptr;
        answerAnalyzeBtn_ = nullptr;
        answerCopyBtn_ = nullptr;
        answerCloseBtn_ = nullptr;
    }
    brain_.DismissDetail();
    if (trackedHwnd_ && IsWindow(trackedHwnd_)) {
        SetForegroundWindow(trackedHwnd_);
    }
}

void OverlayWindow::ShowOrUpdateAnswerWindow(const std::wstring& title, const std::wstring& body) {
    currentAnswerText_ = body;

    // Zeilenumbrüche für Win32 EDIT-Control normalisieren (\n -> \r\n)
    std::wstring editText;
    editText.reserve(body.size() + 32);
    for (size_t i = 0; i < body.size(); ++i) {
        if (body[i] == L'\n' && (i == 0 || body[i - 1] != L'\r')) {
            editText += L"\r\n";
        } else {
            editText.push_back(body[i]);
        }
    }

    if (!answerFont_) {
        answerFont_ = CreateFontW(
            -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    if (!answerBgBrush_) {
        answerBgBrush_ = CreateSolidBrush(RGB(28, 31, 38));
    }

    static bool clsRegistered = false;
    constexpr wchar_t kAnswerCls[] = L"MumpfAnswerPopupClass";
    if (!clsRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = [](HWND h, UINT m, WPARAM wp, LPARAM lp) -> LRESULT {
            auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
            if (m == WM_NCCREATE) {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
                return TRUE;
            }
            if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT) {
                HDC hdc = reinterpret_cast<HDC>(wp);
                SetTextColor(hdc, RGB(238, 242, 248));
                SetBkColor(hdc, RGB(28, 31, 38));
                return reinterpret_cast<LRESULT>(self ? self->answerBgBrush_ : GetStockObject(BLACK_BRUSH));
            }
            if (m == WM_COMMAND && self) {
                if (LOWORD(wp) == kCmdAnswerCopy) {
                    if (OpenClipboard(h)) {
                        EmptyClipboard();
                        const size_t bytes = (self->currentAnswerText_.size() + 1) * sizeof(wchar_t);
                        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
                        if (hMem) {
                            void* dst = GlobalLock(hMem);
                            if (dst) {
                                memcpy(dst, self->currentAnswerText_.c_str(), bytes);
                                GlobalUnlock(hMem);
                                SetClipboardData(CF_UNICODETEXT, hMem);
                            } else {
                                GlobalFree(hMem);
                            }
                        }
                        CloseClipboard();
                    }
                    SetWindowTextW(self->answerCopyBtn_, L"Kopiert!");
                    return 0;
                }
                if (LOWORD(wp) == kCmdAnswerAnalyze) {
                    auto req = self->brain_.StartFileLlmAnalysis();
                    if (req.has_value()) {
                        self->ShowOrUpdateAnswerWindow(self->brain_.DetailTitle(), self->brain_.DetailBody());
                        self->DispatchLlmRequest(*req);
                        self->RenderFrame();
                    }
                    return 0;
                }
                if (LOWORD(wp) == kCmdAnswerClose || LOWORD(wp) == IDCANCEL) {
                    self->CloseAnswerWindow();
                    return 0;
                }
            }
            if (m == WM_CLOSE && self) {
                self->CloseAnswerWindow();
                return 0;
            }
            return DefWindowProcW(h, m, wp, lp);
        };
        wc.hInstance = hInstance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = answerBgBrush_;
        wc.lpszClassName = kAnswerCls;
        RegisterClassExW(&wc);
        clsRegistered = true;
    }

    // Benötigte Höhe exakt aus dem Text berechnen
    const int clientW = 480;
    const int pad = 14;
    const int btnRowH = 36;

    HDC hdc = GetDC(nullptr);
    HGDIOBJ oldFont = SelectObject(hdc, answerFont_);
    RECT calcRect{ 0, 0, clientW - pad * 2 - 22, 0 };
    DrawTextW(hdc, editText.c_str(), static_cast<int>(editText.size()), &calcRect, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL);
    SelectObject(hdc, oldFont);
    ReleaseDC(nullptr, hdc);

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int maxEditH = static_cast<int>((workArea.bottom - workArea.top) * 0.60);
    const int editH = std::clamp(static_cast<int>(calcRect.bottom) + 18, 64, maxEditH);
    const int clientH = pad + editH + 10 + btnRowH + pad;

    RECT wr{ 0, 0, clientW, clientH };
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
    AdjustWindowRectEx(&wr, style, FALSE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
    const int winW = wr.right - wr.left;
    const int winH = wr.bottom - wr.top;

    // Direkt über/neben Mumpf positionieren
    int x = static_cast<int>(posX_) + kCanvasWidth - winW;
    int y = static_cast<int>(posY_) + kSpriteY - winH - 10;
    x = std::clamp(x, static_cast<int>(workArea.left + 12), static_cast<int>(workArea.right - winW - 12));
    y = std::clamp(y, static_cast<int>(workArea.top + 12), static_cast<int>(workArea.bottom - winH - 12));

    if (!answerHwnd_) {
        answerHwnd_ = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            kAnswerCls,
            title.c_str(),
            style,
            x, y, winW, winH,
            hwnd_, nullptr, hInstance_, this);

        BOOL dark = TRUE;
        DwmSetWindowAttribute(answerHwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));

        answerEditHwnd_ = CreateWindowExW(
            0, L"EDIT", editText.c_str(),
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            pad, pad, clientW - pad * 2, editH,
            answerHwnd_, nullptr, hInstance_, nullptr);

        answerAnalyzeBtn_ = CreateWindowExW(
            0, L"BUTTON", L"Mit LLM analysieren",
            WS_CHILD | BS_PUSHBUTTON,
            pad, pad + editH + 10, 155, 28,
            answerHwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCmdAnswerAnalyze)), hInstance_, nullptr);

        answerCopyBtn_ = CreateWindowExW(
            0, L"BUTTON", L"Kopieren",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            clientW - pad - 188, pad + editH + 10, 90, 28,
            answerHwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCmdAnswerCopy)), hInstance_, nullptr);

        answerCloseBtn_ = CreateWindowExW(
            0, L"BUTTON", L"Schließen",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            clientW - pad - 90, pad + editH + 10, 90, 28,
            answerHwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCmdAnswerClose)), hInstance_, nullptr);

        SendMessageW(answerEditHwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(answerFont_), TRUE);
        SendMessageW(answerAnalyzeBtn_, WM_SETFONT, reinterpret_cast<WPARAM>(answerFont_), TRUE);
        SendMessageW(answerCopyBtn_, WM_SETFONT, reinterpret_cast<WPARAM>(answerFont_), TRUE);
        SendMessageW(answerCloseBtn_, WM_SETFONT, reinterpret_cast<WPARAM>(answerFont_), TRUE);

        ShowWindow(answerAnalyzeBtn_, brain_.HasAnalyzableFile() ? SW_SHOW : SW_HIDE);
        ShowWindow(answerHwnd_, SW_SHOWNOACTIVATE);
    } else {
        SetWindowTextW(answerHwnd_, title.c_str());
        SetWindowTextW(answerEditHwnd_, editText.c_str());
        SetWindowTextW(answerCopyBtn_, L"Kopieren");
        SetWindowPos(answerHwnd_, HWND_TOPMOST, x, y, winW, winH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        MoveWindow(answerEditHwnd_, pad, pad, clientW - pad * 2, editH, TRUE);
        MoveWindow(answerAnalyzeBtn_, pad, pad + editH + 10, 155, 28, TRUE);
        ShowWindow(answerAnalyzeBtn_, brain_.HasAnalyzableFile() ? SW_SHOW : SW_HIDE);
        MoveWindow(answerCopyBtn_, clientW - pad - 188, pad + editH + 10, 90, 28, TRUE);
        MoveWindow(answerCloseBtn_, clientW - pad - 90, pad + editH + 10, 90, 28, TRUE);
    }
}

void OverlayWindow::StartBuiltInDemo() {
    demoStep_ = 1;
    demoTimer_ = 0.01f;
}

void OverlayWindow::EnqueueEvent(MumpfEvent ev) {
    {
        std::lock_guard<std::mutex> lock(eventMutex_);
        pendingEvents_.push(std::move(ev));
    }
    if (hwnd_) {
        PostMessageW(hwnd_, kMsgEventReady, 0, 0);
    }
}

void OverlayWindow::DrainEvents() {
    std::queue<MumpfEvent> local;
    {
        std::lock_guard<std::mutex> lock(eventMutex_);
        std::swap(local, pendingEvents_);
    }
    while (!local.empty()) {
        const bool isLlmReply = (local.front().type == L"llm_reply");
        brain_.HandleEvent(local.front());
        if (isLlmReply && brain_.IsDetailOpen()) {
            ShowOrUpdateAnswerWindow(brain_.DetailTitle(), brain_.DetailBody());
        }
        local.pop();
    }
}

void OverlayWindow::UpdateDockTarget(HWND foregroundHwnd) {
    if (!autoDock_ || !foregroundHwnd || foregroundHwnd == hwnd_ || foregroundHwnd == answerHwnd_) return;
    DWORD fgPid = 0;
    GetWindowThreadProcessId(foregroundHwnd, &fgPid);
    if (fgPid == GetCurrentProcessId()) return;
    if (IsIconic(foregroundHwnd) || !IsWindowVisible(foregroundHwnd)) return;

    // Ignoriere Desktop / Taskleiste
    wchar_t cls[64]{};
    GetClassNameW(foregroundHwnd, cls, 64);
    if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0 || wcscmp(cls, L"Shell_TrayWnd") == 0) {
        return;
    }

    RECT bounds{};
    HRESULT hr = DwmGetWindowAttribute(foregroundHwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds));
    if (FAILED(hr)) {
        if (!GetWindowRect(foregroundHwnd, &bounds)) return;
    }

    if ((bounds.right - bounds.left) < 200 || (bounds.bottom - bounds.top) < 150) return;

    trackedHwnd_ = foregroundHwnd;

    RECT workArea{};
    HMONITOR mon = MonitorFromWindow(foregroundHwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        workArea = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    }

    // Mumpf sitzt rechts unten am Rand des aktiven Fensters
    float desiredX = static_cast<float>(bounds.right - kCanvasWidth + 16);
    float desiredY = static_cast<float>(bounds.bottom - kCanvasHeight + 8);

    desiredX = std::clamp(desiredX, static_cast<float>(workArea.left), static_cast<float>(workArea.right - kCanvasWidth));
    desiredY = std::clamp(desiredY, static_cast<float>(workArea.top), static_cast<float>(workArea.bottom - kCanvasHeight));

    targetX_ = desiredX;
    targetY_ = desiredY;
}

void OverlayWindow::OnTimerTick() {
    const ULONGLONG now = GetTickCount64();
    float dt = static_cast<float>(now - lastTickMs_) / 1000.0f;
    lastTickMs_ = now;
    dt = std::clamp(dt, 0.001f, 0.1f);

    // Zuverlässiger Fallback für Fensterwechsel & Fensterbewegung (alle ~200 ms)
    static ULONGLONG lastDockCheckMs = 0;
    if (autoDock_ && !dragging_ && (now - lastDockCheckMs >= 200)) {
        lastDockCheckMs = now;
        HWND fg = GetForegroundWindow();
        if (fg) {
            UpdateDockTarget(fg);
        }
    }

    // Eingebaute Demo-Sequenz (falls über Tray-Menü gestartet)
    if (demoStep_ > 0) {
        demoTimer_ -= dt;
        if (demoTimer_ <= 0.0f) {
            MumpfEvent ev;
            ev.source = L"kr-ide";
            switch (demoStep_) {
                case 1:
                    ev.type = L"build_started";
                    ev.target = L"WINDOWS";
                    brain_.HandleEvent(ev);
                    demoStep_ = 2;
                    demoTimer_ = 2.0f;
                    break;
                case 2:
                    ev.type = L"diagnostic";
                    ev.warnings = 1;
                    ev.diagnostic = DiagnosticInfo{ L"main.kr", 42, L"warning", L"Ungenutzte Variable 'tmp'" };
                    brain_.HandleEvent(ev);
                    demoStep_ = 3;
                    demoTimer_ = 1.6f;
                    break;
                case 3:
                    ev.type = L"diagnostic";
                    ev.warnings = 2;
                    ev.diagnostic = DiagnosticInfo{ L"compute/fft.kr", 19, L"warning", L"Implizite Konvertierung f64 -> f32" };
                    brain_.HandleEvent(ev);
                    demoStep_ = 4;
                    demoTimer_ = 1.8f;
                    break;
                case 4:
                    ev.type = L"build_finished";
                    ev.success = false;
                    ev.warnings = 2;
                    ev.errors = 1;
                    ev.diagnostic = DiagnosticInfo{
                        L"compute/fft.kr", 88, L"error",
                        L"C-ABI Typkonflikt: 'std::string' darf nicht über Rust/C++-Grenze übergeben werden."
                    };
                    brain_.HandleEvent(ev);
                    demoStep_ = 5;
                    demoTimer_ = 6.5f;
                    break;
                case 5:
                    ev.type = L"build_started";
                    ev.target = L"WINDOWS";
                    brain_.HandleEvent(ev);
                    demoStep_ = 6;
                    demoTimer_ = 2.2f;
                    break;
                case 6:
                    ev.type = L"build_finished";
                    ev.success = true;
                    ev.warnings = 0;
                    ev.errors = 0;
                    brain_.HandleEvent(ev);
                    demoStep_ = 0;
                    break;
            }
        }
    }

    // Sanftes Gleiten zum Ziel-Fenster
    if (!dragging_) {
        const float dx = targetX_ - posX_;
        const float dy = targetY_ - posY_;
        const float dist = std::hypot(dx, dy);
        if (dist > 2.5f) {
            const float step = std::min(dist, std::max(180.0f * dt, dist * 8.0f * dt));
            posX_ += (dx / dist) * step;
            posY_ += (dy / dist) * step;

            if (std::abs(dx) > 12.0f) {
                brain_.SetOverrideAnim(dx > 0.0f ? AnimRow::RunningRight : AnimRow::RunningLeft);
            }
        } else {
            posX_ = targetX_;
            posY_ = targetY_;
            brain_.SetOverrideAnim(std::nullopt);
        }
    }

    // Spontane LLM-Sprüche / Witze / Tipps alle 2 Minuten im Leerlauf
    if (idleBanterEnabled_ && llmClient_ && !brain_.IsMuted() &&
        !answerHwnd_ && !brain_.HasBubble() && demoStep_ == 0 && !idleBanterInFlight_) {
        idleBanterTimer_ -= dt;
        if (idleBanterTimer_ <= 0.0f) {
            idleBanterTimer_ = 120.0f;
            idleBanterInFlight_ = true;

            const std::wstring appCtx = !brain_.ActiveWindowTitle().empty()
                ? brain_.ActiveWindowTitle()
                : L"Windows 11 Desktop";

            const int mode = idleBanterMode_++ % 3;
            std::wstring modeTag = L"joke";
            std::wstring prompt;
            if (mode == 0) {
                modeTag = L"joke";
                prompt = L"Erzaehle einen sehr kurzen, flachen, charmanten Witz (genau 1 kurzer Satz, maximal 110 Zeichen) passend zu diesem Programm. Kein Markdown, keine Anfuehrungszeichen.";
            } else if (mode == 1) {
                modeTag = L"tip";
                prompt = L"Gib einen kurzen, nuetzlichen Praxis-Tipp oder Tastatur-Shortcut (genau 1 kurzer Satz, maximal 110 Zeichen) passend zu diesem Programm. Kein Markdown.";
            } else {
                modeTag = L"quip";
                prompt = L"Hau einen lockeren, freundlichen, leicht trockenen Spruch (genau 1 kurzer Satz, maximal 110 Zeichen) ueber das aktuelle Programm heraus. Kein Markdown, keine Anfuehrungszeichen.";
            }

            llmClient_->AskAsync(L"Aktuelles Programm", appCtx, prompt, [this, modeTag](std::wstring reply, bool ok) {
                idleBanterInFlight_ = false;
                if (!ok || reply.empty()) return;
                // Für die dynamische Sprechblase auf eine gut lesbare Länge begrenzen
                if (reply.size() > 260) {
                    reply = reply.substr(0, 257) + L"...";
                }
                MumpfEvent ev;
                ev.source = L"llm";
                ev.type = L"idle_banter";
                ev.target = modeTag;
                ev.text = std::move(reply);
                EnqueueEvent(std::move(ev));
            });
        }
    }

    brain_.Update(dt);
    RenderFrame();
}

void OverlayWindow::RenderFrame() {
    if (!renderTarget_ || !sheetBitmap_) return;

    RECT rc{ 0, 0, kCanvasWidth, kCanvasHeight };
    renderTarget_->BindDC(memDC_, &rc);

    renderTarget_->BeginDraw();
    renderTarget_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    // 1. Mumpf-Sprite zeichnen
    const int row = static_cast<int>(brain_.CurrentAnim());
    const int col = brain_.CurrentFrame();
    const D2D1_RECT_F srcRect = SpriteSheet::GetCellRect(row, col);
    const D2D1_RECT_F dstRect = D2D1::RectF(
        static_cast<float>(kSpriteX),
        static_cast<float>(kSpriteY),
        static_cast<float>(kSpriteX + kSpriteDrawW),
        static_cast<float>(kSpriteY + kSpriteDrawH));

    renderTarget_->DrawBitmap(
        sheetBitmap_.Get(),
        dstRect,
        1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
        srcRect);

    // 2. Detail-Karte (nur Fallback, wenn kein eigenes kopierbares Antwortfenster offen ist)
    if (brain_.IsDetailOpen() && !answerHwnd_) {
        ComPtr<ID2D1SolidColorBrush> bgBrush;
        ComPtr<ID2D1SolidColorBrush> borderBrush;
        ComPtr<ID2D1SolidColorBrush> titleBrush;
        ComPtr<ID2D1SolidColorBrush> bodyBrush;
        ComPtr<ID2D1SolidColorBrush> hintBrush;

        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.11f, 0.12f, 0.15f, 0.95f), &bgBrush);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.88f, 0.36f, 0.32f, 0.95f), &borderBrush);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.82f, 0.55f, 1.0f), &titleBrush);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.94f, 0.95f, 0.98f, 1.0f), &bodyBrush);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.60f, 0.65f, 0.72f, 1.0f), &hintBrush);

        D2D1_ROUNDED_RECT card = D2D1::RoundedRect(
            D2D1::RectF(kDetailX, kDetailY, kDetailX + kDetailW, kDetailY + kDetailH),
            12.0f, 12.0f);
        renderTarget_->FillRoundedRectangle(card, bgBrush.Get());
        renderTarget_->DrawRoundedRectangle(card, borderBrush.Get(), 1.8f);

        D2D1_RECT_F titleRect = D2D1::RectF(kDetailX + 14.0f, kDetailY + 10.0f, kDetailX + kDetailW - 14.0f, kDetailY + 34.0f);
        renderTarget_->DrawTextW(
            brain_.DetailTitle().c_str(),
            static_cast<UINT32>(brain_.DetailTitle().size()),
            titleFont_.Get(),
            titleRect,
            titleBrush.Get());

        D2D1_RECT_F bodyRect = D2D1::RectF(kDetailX + 14.0f, kDetailY + 38.0f, kDetailX + kDetailW - 14.0f, kDetailY + kDetailH - 26.0f);
        renderTarget_->DrawTextW(
            brain_.DetailBody().c_str(),
            static_cast<UINT32>(brain_.DetailBody().size()),
            bodyFont_.Get(),
            bodyRect,
            bodyBrush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);

        const std::wstring hint = L"Klick zum Schließen";
        D2D1_RECT_F hintRect = D2D1::RectF(kDetailX + 14.0f, kDetailY + kDetailH - 24.0f, kDetailX + kDetailW - 14.0f, kDetailY + kDetailH - 6.0f);
        renderTarget_->DrawTextW(
            hint.c_str(),
            static_cast<UINT32>(hint.size()),
            bodyFont_.Get(),
            hintRect,
            hintBrush.Get());
    }
    // 3. Sprechblase (wächst dynamisch nach oben passend zur Textlänge)
    else if (brain_.HasBubble()) {
        ComPtr<ID2D1SolidColorBrush> bgBrush;
        ComPtr<ID2D1SolidColorBrush> borderBrush;
        ComPtr<ID2D1SolidColorBrush> textBrush;

        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.98f, 0.98f, 1.0f, 0.96f), &bgBrush);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.22f, 0.28f, 0.85f), &borderBrush);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.12f, 0.16f, 1.0f), &textBrush);

        const float bx = 16.0f;
        const float bw = 388.0f;
        const float padX = 14.0f;
        const float padY = 12.0f;

        ComPtr<IDWriteTextLayout> layout;
        float textH = 44.0f;
        if (SUCCEEDED(dwriteFactory_->CreateTextLayout(
                brain_.BubbleText().c_str(),
                static_cast<UINT32>(brain_.BubbleText().size()),
                bubbleFont_.Get(),
                bw - padX * 2.0f,
                static_cast<float>(kSpriteY - 24),
                &layout)) && layout) {
            DWRITE_TEXT_METRICS metrics{};
            if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                textH = metrics.height;
            }
        }

        const float maxBh = static_cast<float>(kSpriteY - 14);
        const float bh = std::clamp(textH + padY * 2.0f, 52.0f, maxBh);
        const float by = static_cast<float>(kSpriteY) - bh - 8.0f;

        D2D1_ROUNDED_RECT bubble = D2D1::RoundedRect(D2D1::RectF(bx, by, bx + bw, by + bh), 14.0f, 14.0f);
        renderTarget_->FillRoundedRectangle(bubble, bgBrush.Get());
        renderTarget_->DrawRoundedRectangle(bubble, borderBrush.Get(), 1.5f);

        if (layout) {
            renderTarget_->DrawTextLayout(
                D2D1::Point2F(bx + padX, by + padY),
                layout.Get(),
                textBrush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
        } else {
            D2D1_RECT_F textRect = D2D1::RectF(bx + padX, by + padY, bx + bw - padX, by + bh - padY);
            renderTarget_->DrawTextW(
                brain_.BubbleText().c_str(),
                static_cast<UINT32>(brain_.BubbleText().size()),
                bubbleFont_.Get(),
                textRect,
                textBrush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    renderTarget_->EndDraw();

    POINT ptDst{ static_cast<LONG>(std::round(posX_)), static_cast<LONG>(std::round(posY_)) };
    SIZE szWnd{ kCanvasWidth, kCanvasHeight };
    POINT ptSrc{ 0, 0 };
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd_, nullptr, &ptDst, &szWnd, memDC_, &ptSrc, 0, &blend, ULW_ALPHA);
}

bool OverlayWindow::HitTestSprite(int x, int y) const {
    if (x < kSpriteX || x >= kSpriteX + kSpriteDrawW || y < kSpriteY || y >= kSpriteY + kSpriteDrawH) {
        return false;
    }
    const int localX = (x - kSpriteX) * SpriteSheet::kCellWidth / kSpriteDrawW;
    const int localY = (y - kSpriteY) * SpriteSheet::kCellHeight / kSpriteDrawH;
    return sheet_.IsPixelOpaque(static_cast<int>(brain_.CurrentAnim()), brain_.CurrentFrame(), localX, localY);
}

bool OverlayWindow::HitTestDetailCard(int x, int y) const {
    if (!brain_.IsDetailOpen()) return false;
    return x >= kDetailX && x <= kDetailX + kDetailW && y >= kDetailY && y <= kDetailY + kDetailH;
}

void OverlayWindow::RunMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

LRESULT CALLBACK OverlayWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->WndProc(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CALLBACK OverlayWindow::WinEventProcStatic(
    HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    if (!s_instance_ || !hwnd) return;
    if (event == EVENT_SYSTEM_FOREGROUND) {
        s_instance_->UpdateDockTarget(hwnd);
    } else if (event == EVENT_OBJECT_LOCATIONCHANGE && idObject == OBJID_WINDOW) {
        if (hwnd == s_instance_->trackedHwnd_) {
            s_instance_->UpdateDockTarget(hwnd);
        }
    }
}

LRESULT OverlayWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TIMER:
            if (wParam == kAnimTimerId) {
                OnTimerTick();
            }
            return 0;

        case kMsgEventReady:
            DrainEvents();
            RenderFrame();
            return 0;

        case kMsgTrayIcon:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                ShowTrayMenu();
            } else if (lParam == WM_LBUTTONDBLCLK) {
                StartBuiltInDemo();
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kCmdDemo:
                    StartBuiltInDemo();
                    break;
                case kCmdAutoDock:
                    autoDock_ = !autoDock_;
                    if (autoDock_) UpdateDockTarget(GetForegroundWindow());
                    break;
                case kCmdIdleBanter:
                    idleBanterEnabled_ = !idleBanterEnabled_;
                    if (idleBanterEnabled_) idleBanterTimer_ = 120.0f;
                    break;
                case kCmdMute:
                    brain_.SetMuted(!brain_.IsMuted());
                    break;
                case kCmdExit:
                    PostQuitMessage(0);
                    break;
            }
            return 0;

        case WM_NCHITTEST: {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd_, &pt);
            if (HitTestSprite(pt.x, pt.y) || HitTestDetailCard(pt.x, pt.y)) {
                return HTCLIENT;
            }
            return HTTRANSPARENT;
        }

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (HitTestDetailCard(x, y)) {
                brain_.DismissDetail();
                if (trackedHwnd_ && IsWindow(trackedHwnd_)) {
                    SetForegroundWindow(trackedHwnd_);
                }
                RenderFrame();
                return 0;
            }
            if (HitTestSprite(x, y)) {
                dragging_ = true;
                movedDuringDrag_ = false;
                GetCursorPos(&dragStartScreen_);
                dragStartPosX_ = posX_;
                dragStartPosY_ = posY_;
                SetCapture(hwnd_);
            }
            return 0;
        }

        case WM_MOUSEMOVE:
            if (dragging_) {
                POINT cur{};
                GetCursorPos(&cur);
                const int dx = cur.x - dragStartScreen_.x;
                const int dy = cur.y - dragStartScreen_.y;
                if (std::abs(dx) > 4 || std::abs(dy) > 4) {
                    movedDuringDrag_ = true;
                    autoDock_ = false;
                }
                if (movedDuringDrag_) {
                    posX_ = dragStartPosX_ + static_cast<float>(dx);
                    posY_ = dragStartPosY_ + static_cast<float>(dy);
                    targetX_ = posX_;
                    targetY_ = posY_;
                    RenderFrame();
                }
            }
            return 0;

        case WM_LBUTTONUP:
            if (dragging_) {
                ReleaseCapture();
                dragging_ = false;
                if (!movedDuringDrag_) {
                    if (brain_.HasPendingSelection()) {
                        ShowSelectionActionMenu();
                    } else if (brain_.HasPendingFile()) {
                        ShowExplorerFileActionMenu();
                    } else if (answerHwnd_) {
                        CloseAnswerWindow();
                        RenderFrame();
                    } else if (brain_.IsInBrowser()) {
                        ShowBrowserActionMenu();
                    } else {
                        auto llmReq = brain_.OnClicked();
                        if (brain_.IsDetailOpen()) {
                            ShowOrUpdateAnswerWindow(brain_.DetailTitle(), brain_.DetailBody());
                        }
                        if (llmReq.has_value()) {
                            DispatchLlmRequest(*llmReq);
                        }
                        RenderFrame();
                    }
                }
            }
            return 0;

        case WM_RBUTTONUP:
            ShowTrayMenu();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd_, kAnimTimerId);
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

} // namespace mumpf
