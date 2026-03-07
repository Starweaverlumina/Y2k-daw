#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/session/UndoStack.h
//
//  Command pattern undo/redo stack for the SessionDocument layer.
//
//  Design:
//    • Command is the abstract base. Each mutation is one Command.
//    • apply()  — performs the mutation (also called on redo).
//    • revert() — undoes the mutation.
//    • Commands capture all state needed for both directions.
//
//  UndoStack:
//    • Bounded depth (default 100). Oldest entries are evicted.
//    • execute() applies the command and pushes to history.
//      New commands truncate any pending redo tail.
//    • Consecutive commands of the same logical type can be merged
//      via canMerge() / mergeWith() (e.g. continuous fader drag).
//
//  Thread model: UI thread only. Not RT-safe by design.
// ═══════════════════════════════════════════════════════════════════

#include <memory>
#include <string>
#include <vector>
#include <cassert>

namespace y2k::session {

class SessionDocument; // forward declaration — avoids circular include

// ─────────────────────────────────────────────────────────────────
//  Command — base class for every reversible session mutation
// ─────────────────────────────────────────────────────────────────
class Command {
public:
    virtual ~Command() = default;

    // Apply the operation (redo direction). Called by UndoStack::execute()
    // on first insertion, and again by UndoStack::redo().
    virtual void apply(SessionDocument& doc) = 0;

    // Revert the operation (undo direction). Called by UndoStack::undo().
    virtual void revert(SessionDocument& doc) = 0;

    // Human-readable label for history display and logging.
    virtual std::string description() const = 0;

    // Optional: allow consecutive commands of the same logical kind to merge
    // into one history entry (e.g., repeated fader moves during a drag).
    // If canMerge() returns true, mergeWith() is called BEFORE the new
    // command is pushed. After merging the top entry absorbs the new one;
    // the new command object is discarded.
    virtual bool canMerge(const Command& /*next*/) const noexcept { return false; }
    virtual void mergeWith(std::unique_ptr<Command> /*next*/)      {}
};

// ─────────────────────────────────────────────────────────────────
//  UndoStack — bounded double-ended command history
// ─────────────────────────────────────────────────────────────────
class UndoStack {
public:
    explicit UndoStack(int maxDepth = 100) noexcept
        : maxDepth_(maxDepth > 0 ? maxDepth : 1) {}

    // ── Core operations ───────────────────────────────────────────

    // Execute cmd: apply it, push to history, trim redo tail.
    // If the top command canMerge with this one, they are merged instead
    // of pushing a new entry (the new cmd is absorbed and discarded).
    // Evicts the oldest entry when maxDepth_ is exceeded.
    void execute(std::unique_ptr<Command> cmd, SessionDocument& doc) {
        assert(cmd && "null command passed to UndoStack::execute");

        // Truncate any redo tail first
        truncateRedo();

        // Merge with top if possible
        if (cursor_ >= 0) {
            Command& top = *stack_[static_cast<size_t>(cursor_)];
            if (top.canMerge(*cmd)) {
                top.mergeWith(std::move(cmd));
                // Re-apply the merged command (top.mergeWith updated it)
                top.apply(doc);
                return;
            }
        }

        // Apply before pushing so failure leaves history consistent
        cmd->apply(doc);

        // Evict oldest if we're at capacity
        if (static_cast<int>(stack_.size()) >= maxDepth_) {
            stack_.erase(stack_.begin());
            // cursor_ already points past the evicted slot — keep it in range
            if (cursor_ >= static_cast<int>(stack_.size()))
                cursor_ = static_cast<int>(stack_.size()) - 1;
        }

        stack_.push_back(std::move(cmd));
        cursor_ = static_cast<int>(stack_.size()) - 1;
    }

    bool canUndo() const noexcept { return cursor_ >= 0; }
    bool canRedo() const noexcept {
        return cursor_ < static_cast<int>(stack_.size()) - 1;
    }

    void undo(SessionDocument& doc) {
        if (!canUndo()) return;
        stack_[static_cast<size_t>(cursor_)]->revert(doc);
        --cursor_;
    }

    void redo(SessionDocument& doc) {
        if (!canRedo()) return;
        ++cursor_;
        stack_[static_cast<size_t>(cursor_)]->apply(doc);
    }

    void clear() noexcept {
        stack_.clear();
        cursor_ = -1;
    }

    // ── Introspection ─────────────────────────────────────────────

    // Total entries in history (includes past redo tail)
    int size()   const noexcept { return static_cast<int>(stack_.size()); }

    // Index of the last executed entry (-1 when empty or fully undone)
    int cursor() const noexcept { return cursor_; }

    // Description of what would be undone next (empty string if nothing)
    std::string undoDescription() const {
        if (!canUndo()) return {};
        return stack_[static_cast<size_t>(cursor_)]->description();
    }

    // Description of what would be redone next (empty string if nothing)
    std::string redoDescription() const {
        if (!canRedo()) return {};
        return stack_[static_cast<size_t>(cursor_ + 1)]->description();
    }

private:
    void truncateRedo() {
        if (cursor_ < static_cast<int>(stack_.size()) - 1) {
            stack_.erase(stack_.begin() + cursor_ + 1, stack_.end());
        }
    }

    std::vector<std::unique_ptr<Command>> stack_;
    int cursor_   = -1;
    int maxDepth_ = 100;
};

} // namespace y2k::session
