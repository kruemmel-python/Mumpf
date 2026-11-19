#pragma once

#include "sprite_sheet.h"

#include <chrono>
#include <optional>
#include <string>

namespace mumpf {

struct DiagnosticInfo {
    std::wstring file;
    int line = 0;
    std::wstring severity; // L"warning" oder L"error"
    std::wstring message;
};

struct MumpfEvent {
    std::wstring source;   // z.B. L"kr-ide"
    std::wstring type;     // L"build_started", L"diagnostic", L"build_finished", L"dialog_opened", L"say"
    std::wstring target;   // z.B. L"WINDOWS"
    std::wstring text;     // Freitext / Nachricht
    bool success = false;
    int warnings = 0;
    int errors = 0;
    std::optional<DiagnosticInfo> diagnostic;
};

struct LlmRequest {
    std::wstring title;
    std::wstring body;
    std::wstring instruction;
};

class MumpfBrain {
public:
    void HandleEvent(const MumpfEvent& ev);
    void Update(float dtSeconds);

    // Gibt optional eine Anfrage für das lokale LLM (LM Studio) zurück, wenn der Nutzer Details anfordert.
    std::optional<LlmRequest> OnClicked();
    bool HasPendingSelection() const { return !pendingSelectionText_.empty(); }
    std::optional<LlmRequest> StartSelectionAnalysis(const std::wstring& instruction);
    std::optional<LlmRequest> StartPageAnalysis(const std::wstring& pageContent, const std::wstring& instruction);
    void ClearPendingSelection() { pendingSelectionText_.clear(); }
    bool HasPendingFile() const { return !pendingFilePath_.empty(); }
    bool HasAnalyzableFile() const { return !activeFilePath_.empty(); }
    const std::wstring& PendingFilePath() const { return pendingFilePath_; }
    const std::wstring& PendingFileName() const { return pendingFileName_; }
    void OpenFileProperties();
    std::optional<LlmRequest> StartFileLlmAnalysis();
    void ClearPendingFile() { pendingFilePath_.clear(); pendingFileName_.clear(); }
    bool IsInBrowser() const { return !activeBrowser_.empty(); }
    const std::wstring& ActiveBrowser() const { return activeBrowser_; }
    const std::wstring& ActiveWindowTitle() const { return activeWindowTitle_; }
    void ApplyLlmReply(const std::wstring& reply);
    void DismissDetail();
    void SetMuted(bool muted) { muted_ = muted; }
    bool IsMuted() const { return muted_; }

    AnimRow CurrentAnim() const { return currentAnim_; }
    int CurrentFrame() const { return currentFrame_; }

    bool HasBubble() const { return !bubbleText_.empty() && bubbleRemaining_ > 0.0f; }
    const std::wstring& BubbleText() const { return bubbleText_; }

    bool IsDetailOpen() const { return detailOpen_; }
    const std::wstring& DetailTitle() const { return detailTitle_; }
    const std::wstring& DetailBody() const { return detailBody_; }
    const std::optional<DiagnosticInfo>& LastError() const { return lastError_; }

    void SetOverrideAnim(std::optional<AnimRow> anim);

private:
    void SetAnim(AnimRow row, bool resetFrame = true);
    void Say(const std::wstring& text, float durationSec = 5.5f, bool force = false);

    AnimRow currentAnim_ = AnimRow::Idle;
    AnimRow baseAnim_ = AnimRow::Idle;
    std::optional<AnimRow> overrideAnim_;

    int currentFrame_ = 0;
    float frameTimer_ = 0.0f;
    float stateTimer_ = 0.0f;
    float returnToIdleAfter_ = 0.0f;

    std::wstring bubbleText_;
    float bubbleRemaining_ = 0.0f;

    bool detailOpen_ = false;
    std::wstring detailTitle_;
    std::wstring detailBody_;
    std::optional<DiagnosticInfo> lastError_;

    int activeBuildWarnings_ = 0;
    bool activeBuildRunning_ = false;
    bool muted_ = false;

    std::wstring activeIde_;
    std::wstring activeBrowser_;
    std::wstring activeWindowTitle_;
    std::wstring pendingSelectionText_;
    std::wstring pendingSelectionSource_;
    std::wstring pendingDialogText_;
    std::wstring pendingDialogTitle_;
    std::wstring pendingFilePath_;
    std::wstring pendingFileName_;
    std::wstring activeFilePath_;
    std::wstring activeFileProps_;
};

} // namespace mumpf
