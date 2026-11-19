#pragma once

#include "mumpf_state.h"

#include <windows.h>
#include <atomic>
#include <functional>
#include <thread>

namespace mumpf {

class EventServer {
public:
    using Callback = std::function<void(MumpfEvent)>;

    EventServer() = default;
    ~EventServer();

    bool Start(Callback onEvent);
    void Stop();

    static MumpfEvent ParseJsonLine(const std::string& utf8Line);

private:
    void RunLoop();

    Callback onEvent_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

} // namespace mumpf
