#pragma once

// ═══════════════════════════════════════════════════════════════════
//  Y2K Engine — Undo / Redo System
//  "Time Machine" metaphor: filmstrip history browser,
//  jump to any point, visual state thumbnails.
//  100-step history, intentionally bounded.
// ═══════════════════════════════════════════════════════════════════

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <functional>
#include <memory>
#include <vector>

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  ActionResult  –  returned from perform() for UI feedback
// ─────────────────────────────────────────────────────────────────
enum class ActionResult { Success, Failed, Merged };

// ─────────────────────────────────────────────────────────────────
//  UndoAction  –  a reversible operation
// ─────────────────────────────────────────────────────────────────
class UndoAction {
public:
    virtual ~UndoAction() = default;

    virtual void   undo()  = 0;
    virtual void   redo()  = 0;

    // Y2K: Human-readable description for the history filmstrip
    virtual juce::String description() const = 0;

    // Y2K: Optional — can this action merge with the next one?
    // (e.g., multiple consecutive "Move clip" calls)
    virtual bool canMergeWith(const UndoAction& /*next*/) const { return false; }
    virtual void mergeWith(std::unique_ptr<UndoAction> /*next*/)  {}

    // Y2K: Thumbnail — a mini JPG/PNG of the session state
    // Captured by UndoManager after each action.
    juce::Image thumbnail;
    juce::Time  timestamp = juce::Time::getCurrentTime();
};

// ─────────────────────────────────────────────────────────────────
//  Lambda-based convenience action
// ─────────────────────────────────────────────────────────────────
class LambdaUndoAction : public UndoAction {
public:
    using Fn = std::function<void()>;

    LambdaUndoAction(juce::String desc, Fn undoFn, Fn redoFn)
        : desc_(std::move(desc))
        , undoFn_(std::move(undoFn))
        , redoFn_(std::move(redoFn))
    {}

    void undo() override { if (undoFn_) undoFn_(); }
    void redo() override { if (redoFn_) redoFn_(); }
    juce::String description() const override { return desc_; }

private:
    juce::String desc_;
    Fn undoFn_, redoFn_;
};

// ─────────────────────────────────────────────────────────────────
//  UndoManager
// ─────────────────────────────────────────────────────────────────
class UndoManager {
public:
    explicit UndoManager(int maxHistory = 100);

    // ── Core operations ───────────────────────────────────────────
    ActionResult perform(std::unique_ptr<UndoAction> action);
    bool         canUndo() const noexcept;
    bool         canRedo() const noexcept;
    void         undo();
    void         redo();
    void         clear();

    // ── History access (for Y2K filmstrip UI) ────────────────────
    int  getHistorySize() const noexcept;
    int  getCurrentIndex() const noexcept { return cursor_; }

    struct HistoryEntry {
        juce::String description;
        juce::Time   timestamp;
        juce::Image  thumbnail;  // Populated after capture
        bool         isCurrent;
    };

    std::vector<HistoryEntry> getHistory() const;

    // ── Jump to any history point ─────────────────────────────────
    void revertTo(int historyIndex);

    // ── Thumbnail capture ─────────────────────────────────────────
    // Provide a capture function — called after each action.
    // The function should render a small (160×90) image of the session.
    using ThumbnailCaptureFn = std::function<juce::Image()>;
    void setThumbnailCaptureFn(ThumbnailCaptureFn fn);

    // ── Notifications ─────────────────────────────────────────────
    std::function<void()> onChange;   // Called after any undo/redo/perform

private:
    void truncateRedo();
    void captureThumbnail(UndoAction& action);

    std::vector<std::unique_ptr<UndoAction>> history_;
    int                   cursor_     = -1;  // Points to last performed action
    int                   maxHistory_ = 100;
    ThumbnailCaptureFn    captureFn_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UndoManager)
};

// ─────────────────────────────────────────────────────────────────
//  Built-in actions for common session operations
// ─────────────────────────────────────────────────────────────────

// Move a clip in time
struct MoveClipAction : public UndoAction {
    juce::Uuid clipId;
    juce::Uuid trackId;
    double     oldBeat, newBeat;

    void undo() override; // Implemented in session_actions.cpp
    void redo() override;
    juce::String description() const override;
    bool canMergeWith(const UndoAction& next) const override;
    void mergeWith(std::unique_ptr<UndoAction> next) override;
};

// Adjust a track parameter (volume, pan)
struct TrackParamAction : public UndoAction {
    juce::Uuid   trackId;
    juce::String paramName;
    float        oldValue, newValue;

    void undo() override;
    void redo() override;
    juce::String description() const override;
    bool canMergeWith(const UndoAction& next) const override;
};

// Adjust a plugin parameter
struct PluginParamAction : public UndoAction {
    juce::Uuid trackId;
    uint32_t   nodeId;
    int        paramIdx;
    float      oldValue, newValue;

    void undo() override;
    void redo() override;
    juce::String description() const override;
    bool canMergeWith(const UndoAction& next) const override;
};

// Add/remove a track
struct AddTrackAction : public UndoAction {
    juce::Uuid                              addedTrackId;
    std::function<void(const juce::Uuid&)> doAdd;
    std::function<void(const juce::Uuid&)> doRemove;

    void undo() override { doRemove(addedTrackId); }
    void redo() override { doAdd(addedTrackId); }
    juce::String description() const override { return "Add Track"; }
};

struct RemoveTrackAction : public UndoAction {
    juce::Uuid                              removedTrackId;
    std::function<void(const juce::Uuid&)> doAdd;
    std::function<void(const juce::Uuid&)> doRemove;

    void undo() override { doAdd(removedTrackId); }
    void redo() override { doRemove(removedTrackId); }
    juce::String description() const override { return "Delete Track"; }
};

} // namespace y2k::engine
