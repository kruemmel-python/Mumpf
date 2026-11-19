#pragma once

#include "mumpf_state.h"

#include <windows.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace mumpf {

class UiaObserver {
public:
    using EventCallback = std::function<void(MumpfEvent)>;

    UiaObserver() = default;
    ~UiaObserver();

    bool Start(EventCallback onEvent);
    void Stop();

private:
    void RunLoop();
    std::wstring ReadDialogTextUia(HWND hwnd, void* uiaPtr);
    std::wstring GetProcessName(HWND hwnd);
    std::wstring DetectIdeName(const std::wstring& procName, const std::wstring& title);

    EventCallback onEvent_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

} // namespace mumpf
