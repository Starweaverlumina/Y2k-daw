#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/session/session_schema.h  — v10
//
//  SessionSchema — versioned, forward-compatible session serialization.
//
//  Format: JUCE ValueTree → XML (portable, diff-friendly)
//  Version: schemaVersion field; migrate() upgrades v1.0 → v2.0
//  Atomic save: write .tmp → platformFsync → rename (zero-crash-window)
//  Auto-save: separate autosave.xml, offer recovery if newer than main
// ═══════════════════════════════════════════════════════════════════

#include "../session.h"
#include "../mixer/track_model.h"
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <memory>

namespace y2k::engine {

class SessionSchema {
public:
    static constexpr const char* SCHEMA_VERSION     = "2.0";
    static constexpr const char* MIN_COMPAT_VERSION = "1.0";

    // ── Serialise session + routing → ValueTree ───────────────────
    static juce::ValueTree serialise(const Y2KSession&     session,
                                     const ProjectRouting& routing);

    // ── Deserialise ValueTree → session + routing ─────────────────
    struct LoadResult {
        juce::Result                status       = juce::Result::ok();
        std::unique_ptr<Y2KSession> session;
        ProjectRouting              routing;
        juce::String                migrationLog;  // empty if no migration
    };
    static LoadResult deserialise(const juce::ValueTree& root);

    // ── Atomic save (write .tmp → fsync → rename) ─────────────────
    //  Zero-crash-window: original file untouched until rename succeeds.
    static juce::Result atomicSave(const Y2KSession&     session,
                                   const ProjectRouting& routing,
                                   const juce::File&     destFile);

    // ── Auto-save (best-effort, no rename) ────────────────────────
    static juce::Result autoSave(const Y2KSession&     session,
                                  const ProjectRouting& routing,
                                  const juce::File&     autoSaveFile);

    // ── Recovery: is autosave newer than main project file? ───────
    static bool autoSaveIsNewer(const juce::File& mainFile,
                                const juce::File& autoSaveFile) noexcept;

    // ── Migration (v1.0 → v2.0, mutates root in-place) ───────────
    //  Returns log of changes made. Empty string if already current.
    static juce::String migrate(juce::ValueTree& root);

    // ── Utilities ─────────────────────────────────────────────────
    static juce::String schemaVersion(const juce::ValueTree& root) noexcept;
    static bool         isCompatible (const juce::ValueTree& root) noexcept;

private:
    // Serialise sub-trees
    static juce::ValueTree serialiseTimeline(const Y2KSession&);
    static juce::ValueTree serialiseTracks  (const Y2KSession&);
    static juce::ValueTree serialiseRouting (const ProjectRouting&);
    static juce::ValueTree serialiseMarkers (const Y2KSession&);

    // Deserialise sub-trees
    static void deserialiseTimeline(const juce::ValueTree&, Y2KSession&);
    static void deserialiseTracks  (const juce::ValueTree&, Y2KSession&);
    static void deserialiseRouting (const juce::ValueTree&, ProjectRouting&);
    static void deserialiseMarkers (const juce::ValueTree&, Y2KSession&);

    // Insert / Send serialisation
    static juce::ValueTree         serialiseInserts(const std::vector<InsertSlot>&);
    static juce::ValueTree         serialiseSends  (const std::vector<SendSlot>&);
    static std::vector<InsertSlot> deserialiseInserts(const juce::ValueTree&);
    static std::vector<SendSlot>   deserialiseSends  (const juce::ValueTree&);

    // Platform fsync helper
    static bool platformFsync(const juce::File&) noexcept;
};

} // namespace y2k::engine
