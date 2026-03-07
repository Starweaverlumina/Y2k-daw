#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/session/SessionDocument.h
//
//  SessionDocument — single source of truth for project state.
//
//  Responsibilities:
//    • Owns ProjectMetadata, ProjectRouting (tracks/buses/sends/inserts)
//    • Owns the UndoStack; all mutations go through executeCommand()
//    • Atomic save  (write .tmp → fsync → rename)
//    • Safe load    (schema version check + migration before parse)
//    • Dirty tracking  (set on every mutation, cleared on successful save)
//    • Graph rebuild signal (onRoutingChanged callback)
//
//  Architecture rules:
//    • UI thread only. Never call from the audio callback.
//    • ALL mutations go through executeCommand(). Never mutate
//      routing_ or metadata_ directly from outside the document.
//    • Commands call the mutable accessors below to apply/revert state.
//      Commands fire the appropriate notification via fireSessionChanged()
//      or fireRoutingChanged() at the end of apply()/revert().
//    • onRoutingChanged must trigger GraphCompiler::compile() +
//      graph.prepare() + graph.commitState() in the audio engine.
//
//  Usage:
//    auto doc = SessionDocument::createEmpty();
//    doc->onRoutingChanged = [&]{ audioEngine.rebuildGraph(doc->routing()); };
//    doc->executeCommand(std::make_unique<AddTrackCommand>("Lead", "BondiSynth"));
//    doc->save();
// ═══════════════════════════════════════════════════════════════════

#include "UndoStack.h"
#include "../y2k_engine/session.h"
#include "../y2k_engine/mixer/track_model.h"

#include <juce_core/juce_core.h>
#include <functional>
#include <memory>

namespace y2k::session {

// ─────────────────────────────────────────────────────────────────
//  ProjectMetadata — human-visible project information
// ─────────────────────────────────────────────────────────────────
struct ProjectMetadata {
    juce::String projectName  = "Untitled";
    juce::String author;
    juce::String dawVersion   = "1.0.0";
    juce::Time   createdDate  = juce::Time::getCurrentTime();
    juce::Time   modifiedDate = juce::Time::getCurrentTime();

    // Timeline settings
    double tempo      = 120.0;   // BPM, clamped 20–999
    int    timeSigNum = 4;
    int    timeSigDen = 4;
    int    sampleRate = 48000;   // 44100, 48000, 88200, 96000
    int    bitDepth   = 24;      // 16, 24, 32
    double totalBeats = 128.0;
};

// ─────────────────────────────────────────────────────────────────
//  LoadResult — returned by SessionDocument::load()
// ─────────────────────────────────────────────────────────────────
struct LoadResult {
    bool         ok           = false;
    juce::String errorMessage;
    juce::String migrationLog; // non-empty if schema was migrated on load
};

// ─────────────────────────────────────────────────────────────────
//  SessionDocument
// ─────────────────────────────────────────────────────────────────
class SessionDocument {
public:
    // ── Factory ───────────────────────────────────────────────────

    // Create a new empty document with default metadata and no tracks.
    static std::unique_ptr<SessionDocument> createEmpty();

    // Load a previously saved document from path.
    // On success the document is ready to use and isDirty() == false.
    // On failure returns a document in an error state — check the result.
    static std::unique_ptr<SessionDocument> load(const juce::File& projectFile,
                                                   LoadResult&       outResult);

    // ── Save ──────────────────────────────────────────────────────

    // Atomic save to the current savePath_ (the file last loaded/saved).
    // Returns false and fills errorOut if save fails.
    // On success: isDirty() is cleared.
    bool save(juce::String* errorOut = nullptr);

    // Save to a new location, then update savePath_.
    bool saveAs(const juce::File& newFile, juce::String* errorOut = nullptr);

    // Whether a save path has been set (false for newly created documents
    // that have never been saved).
    bool hasSavePath() const noexcept { return savePath_.existsAsFile() ||
                                               !savePath_.getFullPathName().isEmpty(); }

    const juce::File& savePath() const noexcept { return savePath_; }

    // ── Command execution (all mutations go here) ─────────────────

    void executeCommand(std::unique_ptr<Command> cmd);

    // ── Undo / Redo ───────────────────────────────────────────────

    bool canUndo() const noexcept { return undoStack_.canUndo(); }
    bool canRedo() const noexcept { return undoStack_.canRedo(); }
    void undo();
    void redo();

    // Descriptions for UI labelling ("Undo Add Track", etc.)
    std::string undoDescription() const { return undoStack_.undoDescription(); }
    std::string redoDescription() const { return undoStack_.redoDescription(); }

    // Drop all history (e.g. after loading a file)
    void clearUndoHistory() noexcept { undoStack_.clear(); }

    // ── State access (read-only for non-Command consumers) ────────

    const ProjectMetadata&         metadata() const noexcept { return metadata_; }
    const y2k::engine::ProjectRouting& routing()  const noexcept { return routing_;  }

    // ── Mutable access — for use by Commands ONLY ─────────────────
    // Direct callers outside Commands should use executeCommand() instead.

    ProjectMetadata&              mutableMetadata() noexcept { return metadata_; }
    y2k::engine::ProjectRouting&  mutableRouting()  noexcept { return routing_;  }

    // Convenience track finder (linear scan by string UUID).
    // Returns nullptr if not found.
    y2k::engine::TrackModel* findTrack(const juce::String& trackId) noexcept;

    // ── Dirty state ───────────────────────────────────────────────

    bool isDirty() const noexcept { return dirty_; }
    void markDirty();
    void clearDirty() noexcept { dirty_ = false; }

    // ── Notification hooks ────────────────────────────────────────
    // Wire these before calling executeCommand / load.

    // Fired after any state change (metadata, routing, or both).
    // Consumers: UI redraw.
    std::function<void()> onSessionChanged;

    // Fired when the routing topology changes (track added/removed,
    // bus added/removed, send re-wired, insert chain changed).
    // Consumer: audio engine must call GraphCompiler::compile() then
    //           graph.prepare() + graph.commitState().
    std::function<void()> onRoutingChanged;

    // Fired when isDirty() changes value (true → false or false → true).
    // Consumer: window title bar ("●" dirty indicator).
    std::function<void()> onDirtyChanged;

    // ── Notification helpers (called by Commands) ─────────────────

    // General session data changed (metadata, tempo, etc.)
    void fireSessionChanged();

    // Routing topology changed — implies fireSessionChanged() too.
    void fireRoutingChanged();

private:
    SessionDocument() = default;

    // Sync metadata_ → underlying Y2KSession used for serialisation
    void syncToSession() const;

    // Sync loaded Y2KSession → metadata_
    void syncFromSession(const y2k::engine::Y2KSession& s);

    // ── Owned state ───────────────────────────────────────────────
    ProjectMetadata              metadata_;
    y2k::engine::ProjectRouting  routing_;
    UndoStack                    undoStack_;
    juce::File                   savePath_;
    bool                         dirty_ = false;

    // Transient Y2KSession used only for serialisation (not primary state)
    mutable std::unique_ptr<y2k::engine::Y2KSession> serSession_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionDocument)
};

} // namespace y2k::session
