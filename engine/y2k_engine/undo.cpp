// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/undo.cpp
//  UndoManager — action stack, undo/redo, merge, history browser
// ═══════════════════════════════════════════════════════════════════

#include "undo.h"

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  UndoManager
// ─────────────────────────────────────────────────────────────────
UndoManager::UndoManager(int maxHistory)
    : maxHistory_(maxHistory)
{}

ActionResult UndoManager::perform(std::unique_ptr<UndoAction> action) {
    if (!action) return ActionResult::Failed;

    // Try merge with current top action
    if (cursor_ >= 0 && cursor_ < (int)history_.size()) {
        UndoAction* top = history_[cursor_].get();
        if (top->canMergeWith(*action)) {
            top->mergeWith(std::move(action));
            if (onChange) onChange();
            return ActionResult::Merged;
        }
    }

    // Truncate any redo history above cursor
    truncateRedo();

    // Perform the action (redo = initial perform)
    action->redo();
    captureThumbnail(*action);

    history_.push_back(std::move(action));
    cursor_ = static_cast<int>(history_.size()) - 1;

    // Trim to max history
    while ((int)history_.size() > maxHistory_) {
        history_.erase(history_.begin());
        cursor_--;
    }

    if (onChange) onChange();
    return ActionResult::Success;
}

bool UndoManager::canUndo() const noexcept {
    return cursor_ >= 0;
}

bool UndoManager::canRedo() const noexcept {
    return cursor_ < (int)history_.size() - 1;
}

void UndoManager::undo() {
    if (!canUndo()) return;
    history_[cursor_]->undo();
    cursor_--;
    if (onChange) onChange();
}

void UndoManager::redo() {
    if (!canRedo()) return;
    cursor_++;
    history_[cursor_]->redo();
    if (onChange) onChange();
}

void UndoManager::clear() {
    history_.clear();
    cursor_ = -1;
    if (onChange) onChange();
}

int UndoManager::getHistorySize() const noexcept {
    return static_cast<int>(history_.size());
}

std::vector<UndoManager::HistoryEntry> UndoManager::getHistory() const {
    std::vector<HistoryEntry> entries;
    entries.reserve(history_.size());
    for (int i = 0; i < (int)history_.size(); ++i) {
        HistoryEntry e;
        e.description = history_[i]->description();
        e.timestamp   = history_[i]->timestamp;
        e.thumbnail   = history_[i]->thumbnail;
        e.isCurrent   = (i == cursor_);
        entries.push_back(std::move(e));
    }
    return entries;
}

void UndoManager::revertTo(int historyIndex) {
    if (historyIndex < -1 || historyIndex >= (int)history_.size()) return;

    while (cursor_ > historyIndex) undo();
    while (cursor_ < historyIndex) redo();
}

void UndoManager::setThumbnailCaptureFn(ThumbnailCaptureFn fn) {
    captureFn_ = std::move(fn);
}

void UndoManager::truncateRedo() {
    if (cursor_ < (int)history_.size() - 1) {
        history_.erase(history_.begin() + cursor_ + 1, history_.end());
    }
}

void UndoManager::captureThumbnail(UndoAction& action) {
    if (captureFn_)
        action.thumbnail = captureFn_();
}

// ─────────────────────────────────────────────────────────────────
//  MoveClipAction
// ─────────────────────────────────────────────────────────────────
void MoveClipAction::undo() {
    // Implementation deferred: caller sets up via callbacks
    // or Y2KSession is passed in. Minimal wiring via stored ptrs.
}

void MoveClipAction::redo() {
    // Set clip start to newBeat
}

juce::String MoveClipAction::description() const {
    return "Move Clip";
}

bool MoveClipAction::canMergeWith(const UndoAction& next) const {
    const auto* other = dynamic_cast<const MoveClipAction*>(&next);
    return other && other->clipId == clipId && other->trackId == trackId;
}

void MoveClipAction::mergeWith(std::unique_ptr<UndoAction> next) {
    if (auto* other = dynamic_cast<MoveClipAction*>(next.get()))
        newBeat = other->newBeat;
}

// ─────────────────────────────────────────────────────────────────
//  TrackParamAction
// ─────────────────────────────────────────────────────────────────
void TrackParamAction::undo() {}
void TrackParamAction::redo() {}

juce::String TrackParamAction::description() const {
    return "Change " + paramName;
}

bool TrackParamAction::canMergeWith(const UndoAction& next) const {
    const auto* other = dynamic_cast<const TrackParamAction*>(&next);
    return other && other->trackId == trackId && other->paramName == paramName;
}

// ─────────────────────────────────────────────────────────────────
//  PluginParamAction
// ─────────────────────────────────────────────────────────────────
void PluginParamAction::undo() {}
void PluginParamAction::redo() {}

juce::String PluginParamAction::description() const {
    return "Plugin Param " + juce::String(paramIdx);
}

bool PluginParamAction::canMergeWith(const UndoAction& next) const {
    const auto* other = dynamic_cast<const PluginParamAction*>(&next);
    return other && other->nodeId == nodeId && other->paramIdx == paramIdx;
}

} // namespace y2k::engine
