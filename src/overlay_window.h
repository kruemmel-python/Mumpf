#pragma once

#include "llm_client.h"
#include "mumpf_state.h"
#include "sprite_sheet.h"

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <mutex>
#include <queue>
#include <string>

namespace mumpf {

class OverlayWindow {
public:
    static constexpr int kCanvasWidth = 420;
    static constexpr int kCanvasHeight = 400;
    static constexpr int kSpriteDrawW = 144;
    static constexpr int kSpriteDrawH = 156;

    OverlayWindow() = default;
    ~OverlayWindow();

    bool Create(HINSTANCE hInstance, const std::wstring& spritesheetPath);
    void SetLlmClient(const LlmClient* llm) { llmClient_ = llm; }
    void EnqueueEvent(MumpfEvent ev);
    void RunMessageLoop();

private:
    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void CALLBACK WinEventProcStatic(
        HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
        DWORD dwEventThread, DWORD dwmsEventTime);

    LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
    bool InitGraphics();
    void InitTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu();
    void ShowSelectionActionMenu();
    void ShowBrowserActionMenu();
    void ShowExplorerFileActionMenu();
    std::wstring PickTargetFolder(const std::wstring& title);
    std::wstring ReadBrowserPageSnippet(HWND browserHwnd);
    void DispatchLlmRequest(const LlmRequest& req);
    std::wstring PromptCustomInstruction();
    void ShowOrUpdateAnswerWindow(const std::wstring& title, const std::wstring& body);
    void CloseAnswerWindow();
    void StartBuiltInDemo();

    void OnTimerTick();
    void DrainEvents();
    void UpdateDockTarget(HWND foregroundHwnd);
    void RenderFrame();

    bool HitTestSprite(int x, int y) const;
    bool HitTestDetailCard(int x, int y) const;

    HINSTANCE hInstance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND answerHwnd_ = nullptr;
    HWND answerEditHwnd_ = nullptr;
    HWND answerAnalyzeBtn_ = nullptr;
    HWND answerCopyBtn_ = nullptr;
    HWND answerCloseBtn_ = nullptr;
    HFONT answerFont_ = nullptr;
    HBRUSH answerBgBrush_ = nullptr;
    std::wstring currentAnswerText_;
    HWINEVENTHOOK winEventHook_ = nullptr;
    NOTIFYICONDATAW trayIcon_{};
    bool trayAdded_ = false;

    SpriteSheet sheet_;
    MumpfBrain brain_;
    const LlmClient* llmClient_ = nullptr;

    // GDI DIB-Section + Direct2D DC RenderTarget für UpdateLayeredWindow
    HDC memDC_ = nullptr;
    HBITMAP dibBitmap_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    void* dibBits_ = nullptr;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> sheetBitmap_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> bubbleFont_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFont_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> bodyFont_;

    // Position & Docking
    float posX_ = 100.0f;
    float posY_ = 100.0f;
    float targetX_ = 100.0f;
    float targetY_ = 100.0f;
    bool autoDock_ = true;
    HWND trackedHwnd_ = nullptr;

    // Dragging
    bool dragging_ = false;
    bool movedDuringDrag_ = false;
    POINT dragStartScreen_{};
    float dragStartPosX_ = 0.0f;
    float dragStartPosY_ = 0.0f;

    // Demo-Sequenz (über Tray-Menü auslösbar)
    int demoStep_ = 0;
    float demoTimer_ = 0.0f;

    // Spontane LLM-Sprüche / Witze / Tipps alle 2 Minuten im Leerlauf
    bool idleBanterEnabled_ = true;
    bool idleBanterInFlight_ = false;
    float idleBanterTimer_ = 120.0f;
    int idleBanterMode_ = 0;

    // Thread-safe Event-Queue
    std::mutex eventMutex_;
    std::queue<MumpfEvent> pendingEvents_;

    ULONGLONG lastTickMs_ = 0;

    static OverlayWindow* s_instance_;
};

} // namespace mumpf
