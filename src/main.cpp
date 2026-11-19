#include "event_server.h"
#include "llm_client.h"
#include "overlay_window.h"
#include "uia_observer.h"

#include <windows.h>
#include <objbase.h>

#include <filesystem>
#include <string>

namespace {

std::wstring FindAssetPath(const std::wstring& filename) {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path dir = std::filesystem::path(exePath).parent_path();

    const std::wstring candidates[] = {
        (dir / filename).wstring(),
        (dir.parent_path() / filename).wstring(),
        (dir.parent_path().parent_path() / filename).wstring(),
        (std::filesystem::path(L"H:\\mumpf_for_Windows") / filename).wstring()
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p;
        }
    }
    return candidates[0];
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    HANDLE singleInstance = CreateMutexW(nullptr, TRUE, L"Local\\MumpfWindowsHelperSingleton");
    if (singleInstance && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(singleInstance);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    mumpf::LlmClient llmClient;
    llmClient.Configure(mumpf::LlmClient::LoadConfigFromFile(FindAssetPath(L"mumpf.config.json")));

    mumpf::OverlayWindow overlay;
    overlay.SetLlmClient(&llmClient);
    if (!overlay.Create(hInstance, FindAssetPath(L"mumpf-spritesheet-v2-final.png"))) {
        CoUninitialize();
        if (singleInstance) CloseHandle(singleInstance);
        return 1;
    }

    mumpf::EventServer server;
    server.Start([&overlay](mumpf::MumpfEvent ev) {
        overlay.EnqueueEvent(std::move(ev));
    });

    mumpf::UiaObserver uiaObserver;
    uiaObserver.Start([&overlay](mumpf::MumpfEvent ev) {
        overlay.EnqueueEvent(std::move(ev));
    });

    overlay.RunMessageLoop();

    uiaObserver.Stop();
    server.Stop();
    CoUninitialize();
    if (singleInstance) CloseHandle(singleInstance);
    return 0;
}
