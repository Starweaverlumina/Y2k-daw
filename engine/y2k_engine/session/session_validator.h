#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/session/session_validator.h  — v10
//
//  SessionValidator — strict load-time validation + migration.
//
//  Pipeline (always run in this order):
//    1. parse JSON           → SessionData
//    2. check schema version → migrate if needed
//    3. validate routing     → catch broken references
//    4. compile graph        → only if all above pass
//
//  Validator catches:
//    • Null or empty IDs (tracks, buses)
//    • Duplicate IDs (tracks, buses)
//    • Send targets that reference nonexistent buses
//    • OutputBus references that don't exist
//    • Impossible transport values (BPM < 20, timeSig = 0)
//    • Missing required fields (masterBus null ID)
//    • Schema version newer than we know how to read
//
//  Migration:
//    • Each migration step upgrades schema N → N+1
//    • Migrations operate on raw juce::var JSON trees (pre-deserialization)
//    • Registered in order; chain runs until current version reached
//    • Missing fields get sensible defaults; nothing is dropped
//
//  Usage:
//    SessionData data;
//    if (!SessionSerializer::fromJsonString(json, data)) { ... }
//    auto vr = SessionValidator::validateAndMigrate(data);
//    if (!vr.wasOk()) { show error; return; }
//    ProjectRouting routing;
//    SessionSerializer::routingFromJson(data.routingJson, routing);
// ═══════════════════════════════════════════════════════════════════

#include "session_schema.h"
#include "../mixer/track_model.h"
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  ValidationError  — detailed problem report
// ─────────────────────────────────────────────────────────────────
struct ValidationError {
    enum class Level { Warning, Error };
    Level        level   = Level::Error;
    juce::String field;    // e.g. "tracks[0].sends[1].targetBusId"
    juce::String message;
};

struct ValidationReport {
    std::vector<ValidationError> errors;
    std::vector<ValidationError> warnings;
    bool hasErrors()   const noexcept { return !errors.empty(); }
    bool hasWarnings() const noexcept { return !warnings.empty(); }
    juce::Result toResult() const;
    juce::String summary() const;
};

// ─────────────────────────────────────────────────────────────────
//  SessionMigration  — one step in the migration chain
// ─────────────────────────────────────────────────────────────────
struct SessionMigration {
    juce::String fromVersion; // e.g. "0.9"
    juce::String toVersion;   // e.g. "1.0"
    juce::String description;

    // Mutates the raw JSON var in place.
    // Returns false if migration fails critically.
    using MigrateFn = std::function<bool(juce::var& root)>;
    MigrateFn migrate;
};

// ─────────────────────────────────────────────────────────────────
//  SessionValidator
// ─────────────────────────────────────────────────────────────────
class SessionValidator {
public:
    // ── Main entry point ──────────────────────────────────────────
    //
    //  Runs in order:
    //    1. Version check
    //    2. Migration (if needed)
    //    3. Structural validation
    //    4. Routing consistency validation
    //
    //  Mutates data.routingJson in place during migration.
    //  Returns ok() if no errors (warnings are non-fatal).
    static juce::Result validateAndMigrate(SessionData& data);

    // ── Individual phases (also usable standalone) ────────────────

    // Check schema version; returns fail if too new to read.
    static juce::Result checkVersion(const SessionData& data);

    // Run migration chain from data.meta.schemaVersion → kSchemaVersion.
    // Mutates data.routingJson. Returns fail if any migration step fails.
    static juce::Result migrate(SessionData& data);

    // Full routing validation on already-migrated SessionData.
    // Does NOT require a compiled graph.
    static ValidationReport validate(const SessionData& data);

    // Validate a ProjectRouting directly (used by GraphCompiler and tests).
    // This is the post-deserialization validation layer.
    static ValidationReport validateRouting(const ProjectRouting& routing);

    // ── Migration registry ────────────────────────────────────────
    // Returns all registered migrations in order.
    static const std::vector<SessionMigration>& migrations();

private:
    static void validateRoutingJson(const juce::var& routing,
                                     ValidationReport& report);
    static void validateTrackJson(const juce::var& track, int idx,
                                   const juce::StringArray& busIds,
                                   ValidationReport& report);
    static void validateBusJson(const juce::var& bus, int idx,
                                 ValidationReport& report);
    static void validateSendJson(const juce::var& send, const juce::String& trackName,
                                  int sendIdx, const juce::StringArray& busIds,
                                  ValidationReport& report);
    static void validateTransport(const SessionTransportData& t,
                                   ValidationReport& report);
};

} // namespace y2k::engine
