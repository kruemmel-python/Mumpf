#pragma once

#include <functional>
#include <string>

namespace mumpf {

struct LlmConfig {
    bool enabled = true;
    std::wstring host = L"127.0.0.1";
    int port = 1234;
    std::string model = "auto";
};

class LlmClient {
public:
    using ReplyCallback = std::function<void(std::wstring reply, bool ok)>;

    void Configure(const LlmConfig& cfg) { config_ = cfg; }
    const LlmConfig& Config() const { return config_; }

    static LlmConfig LoadConfigFromFile(const std::wstring& path);

    // Sendet eine asynchrone Anfrage an LM Studio (/v1/chat/completions) und ruft onReply auf.
    void AskAsync(
        const std::wstring& contextTitle,
        const std::wstring& contextBody,
        const std::wstring& customInstruction,
        ReplyCallback onReply) const;

private:
    LlmConfig config_;
};

} // namespace mumpf
