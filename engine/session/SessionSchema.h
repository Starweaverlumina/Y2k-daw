#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/session/SessionSchema.h
//
//  Canonical session document schema — single source of truth for
//  the serialized file format produced by SessionDocument.
//
//  Schema format: JUCE ValueTree → UTF-8 XML
//  File extension: .y2k  (bundle directory containing project.y2k)
//
//  Version history:
//    1.0  — initial routing + metadata
//    2.0  — adds Routing sub-tree (buses, sends, inserts)
//           (current CURRENT_VERSION)
//
//  Migration policy:
//    Files with schemaVersion >= MIN_READABLE_VERSION are accepted.
//    migrate() upgrades older files in-place before deserialisation.
//    Files newer than CURRENT_VERSION are rejected to avoid silent
//    data loss.
//
//  Atomic save guarantee (write .tmp → fsync → rename):
//    The original project file is never touched until the rename
//    succeeds. A crash anywhere before rename leaves the original
//    intact. See atomicSave() in SessionDocument.cpp.
//
//  Delegates to y2k::engine::SessionSchema for the actual
//  ValueTree serialisation / migration logic.
// ═══════════════════════════════════════════════════════════════════

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <string_view>

// Pull in the underlying engine-layer schema (migration + serialisation)
#include "../y2k_engine/session/session_schema.h"
#include "../y2k_engine/session.h"
#include "../y2k_engine/mixer/track_model.h"

namespace y2k::session {

// ─────────────────────────────────────────────────────────────────
//  Version constants
// ─────────────────────────────────────────────────────────────────
inline constexpr int SCHEMA_MAJOR         = 2;
inline constexpr int SCHEMA_MINOR         = 0;
inline constexpr const char* CURRENT_VERSION   = "2.0";
inline constexpr const char* MIN_READABLE_VERSION = "1.0";

// ─────────────────────────────────────────────────────────────────
//  Field names — authoritative list.
//  All serialisation code MUST reference these constants instead of
//  raw string literals so renames stay in one place.
// ─────────────────────────────────────────────────────────────────
struct Fields {
    // ── Root ────────────────────────────────────────────────────
    static constexpr std::string_view SCHEMA_VERSION   = "schemaVersion";
    static constexpr std::string_view PROJECT_NAME     = "projectName";
    static constexpr std::string_view AUTHOR           = "author";
    static constexpr std::string_view DAW_VERSION      = "dawVersion";
    static constexpr std::string_view CREATED_DATE     = "createdDate";
    static constexpr std::string_view MODIFIED_DATE    = "modifiedDate";

    // ── Timeline ────────────────────────────────────────────────
    static constexpr std::string_view TIMELINE         = "timeline";
    static constexpr std::string_view TEMPO            = "tempo";
    static constexpr std::string_view TIME_SIG_NUM     = "timeSigNum";
    static constexpr std::string_view TIME_SIG_DEN     = "timeSigDen";
    static constexpr std::string_view SAMPLE_RATE      = "sampleRate";
    static constexpr std::string_view BIT_DEPTH        = "bitDepth";
    static constexpr std::string_view TOTAL_BEATS      = "totalBeats";

    // ── Routing root ────────────────────────────────────────────
    static constexpr std::string_view ROUTING          = "routing";
    static constexpr std::string_view TRACKS           = "tracks";
    static constexpr std::string_view BUSES            = "buses";
    static constexpr std::string_view MASTER_BUS       = "masterBus";

    // ── Track / Bus shared ──────────────────────────────────────
    static constexpr std::string_view TRACK            = "track";
    static constexpr std::string_view BUS              = "bus";
    static constexpr std::string_view ID               = "id";
    static constexpr std::string_view NAME             = "name";
    static constexpr std::string_view VOLUME           = "volume";
    static constexpr std::string_view PAN              = "pan";
    static constexpr std::string_view MUTED            = "muted";
    static constexpr std::string_view SOLOED           = "soloed";
    static constexpr std::string_view IS_MASTER        = "isMaster";
    static constexpr std::string_view METER_TAP        = "meterTap";

    // ── Track-specific ──────────────────────────────────────────
    static constexpr std::string_view INPUT_TYPE       = "inputType";
    static constexpr std::string_view INSTRUMENT_ID    = "instrumentId";
    static constexpr std::string_view AUDIO_IN_CH_L    = "audioInChL";
    static constexpr std::string_view AUDIO_IN_CH_R    = "audioInChR";
    static constexpr std::string_view OUTPUT_BUS_ID    = "outputBusId";

    // ── Inserts ─────────────────────────────────────────────────
    static constexpr std::string_view INSERTS          = "inserts";
    static constexpr std::string_view INSERT           = "insert";
    static constexpr std::string_view TYPE_ID          = "typeId";
    static constexpr std::string_view BYPASSED         = "bypassed";
    static constexpr std::string_view PARAMS           = "params";
    static constexpr std::string_view PARAM            = "param";
    static constexpr std::string_view PARAM_IDX        = "idx";
    static constexpr std::string_view PARAM_VAL        = "value";

    // ── Sends ───────────────────────────────────────────────────
    static constexpr std::string_view SENDS            = "sends";
    static constexpr std::string_view SEND             = "send";
    static constexpr std::string_view TARGET_BUS_ID    = "targetBusId";
    static constexpr std::string_view LEVEL            = "level";
    static constexpr std::string_view PRE_FADER        = "preFader";
    static constexpr std::string_view ENABLED          = "enabled";
};

// ─────────────────────────────────────────────────────────────────
//  SchemaVersion — comparable version tag parsed from file
// ─────────────────────────────────────────────────────────────────
struct SchemaVersion {
    int major = 0;
    int minor = 0;

    constexpr bool operator==(const SchemaVersion& rhs) const noexcept = default;
    constexpr bool operator<(const SchemaVersion& rhs)  const noexcept {
        return major < rhs.major || (major == rhs.major && minor < rhs.minor);
    }
    constexpr bool operator<=(const SchemaVersion& rhs) const noexcept {
        return !(rhs < *this);
    }
    constexpr bool operator>(const SchemaVersion& rhs)  const noexcept {
        return rhs < *this;
    }

    // Parse "2.0" → {2, 0}. Returns {0,0} on parse error.
    static SchemaVersion parse(const juce::String& sv) noexcept {
        const int dot = sv.indexOfChar('.');
        if (dot < 0) return {};
        return { sv.substring(0, dot).getIntValue(),
                 sv.substring(dot + 1).getIntValue() };
    }

    juce::String toString() const {
        return juce::String(major) + "." + juce::String(minor);
    }
};

inline constexpr SchemaVersion CURRENT_SCHEMA_VERSION  { SCHEMA_MAJOR, SCHEMA_MINOR };
inline constexpr SchemaVersion MIN_COMPATIBLE_VERSION  { 1, 0 };

// ─────────────────────────────────────────────────────────────────
//  Migration helpers
// ─────────────────────────────────────────────────────────────────

// Returns a log string describing every change made.
// Empty string if no migration was necessary.
// Mutates root in-place; call before deserialising.
inline juce::String migrateRoot(juce::ValueTree& root) {
    // Delegate to the engine-layer migration which already handles
    // v1.0 → v2.0 (adds Routing sub-tree).
    return y2k::engine::SessionSchema::migrate(root);
}

// True if this file version can be loaded by the current code.
inline bool isCompatibleVersion(const juce::ValueTree& root) noexcept {
    return y2k::engine::SessionSchema::isCompatible(root);
}

// Read the schemaVersion string from a root ValueTree.
inline SchemaVersion readVersion(const juce::ValueTree& root) noexcept {
    return SchemaVersion::parse(
        y2k::engine::SessionSchema::schemaVersion(root));
}

} // namespace y2k::session
