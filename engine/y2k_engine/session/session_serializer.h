#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/session/session_serializer.h  — v10
//
//  SessionSerializer — ProjectRouting ↔ JSON, SessionData ↔ JSON.
//
//  Design rules:
//    • All serialization is synchronous and deterministic
//    • No runtime artifacts (pointers, node IDs, atomic values) serialized
//    • JSON produced is human-readable (pretty-printed with 2-space indent)
//    • Serialization never throws — errors returned as juce::Result
//    • Field names sourced exclusively from session_schema.h constants
//
//  Usage:
//    // Serialize:
//    auto result = SessionSerializer::toJson(routing, meta, transport);
//    if (result.ok) writeFile(result.json);
//
//    // Deserialize:
//    auto routing = SessionSerializer::routingFromJson(json["routing"]);
// ═══════════════════════════════════════════════════════════════════

#include "session_schema.h"
#include "../mixer/track_model.h"
#include <juce_core/juce_core.h>

namespace y2k::engine {

class SessionSerializer {
public:
    // ── Full session ↔ JSON ────────────────────────────────────────

    // Serialize entire project to a JSON string.
    // Returns empty string on failure (details logged via juce::Logger).
    static juce::String toJsonString(
        const ProjectRouting&   routing,
        const SessionMetaData&  meta,
        const SessionTransportData& transport,
        const juce::Array<juce::var>& markers = {});

    // Parse a JSON string into SessionData.
    // Does NOT validate or migrate — call SessionValidator afterward.
    static bool fromJsonString(
        const juce::String& json,
        SessionData&        outData);

    // ── Routing ↔ JSON ─────────────────────────────────────────────

    static juce::var routingToJson(const ProjectRouting& routing);
    static bool      routingFromJson(const juce::var& json, ProjectRouting& out);

    // ── Track / Bus ────────────────────────────────────────────────

    static juce::var trackToJson(const TrackModel& t);
    static bool      trackFromJson(const juce::var& json, TrackModel& out);

    static juce::var busToJson(const BusModel& b);
    static bool      busFromJson(const juce::var& json, BusModel& out);

    // ── Sub-objects ────────────────────────────────────────────────

    static juce::var insertToJson(const InsertSlot& ins);
    static bool      insertFromJson(const juce::var& json, InsertSlot& out);

    static juce::var sendToJson(const SendSlot& send);
    static bool      sendFromJson(const juce::var& json, SendSlot& out);

    static juce::var autoLanesToJson(const std::vector<AutomationLane>& lanes);
    static bool      autoLanesFromJson(const juce::var& json,
                                        std::vector<AutomationLane>& out);

    // ── Helpers ────────────────────────────────────────────────────

    // Convert MeterTapPoint enum ↔ string constant
    static const char*  tapToStr(MeterTapPoint tap) noexcept;
    static MeterTapPoint tapFromStr(const juce::String& s) noexcept;

    // Convert TrackInputType enum ↔ string constant
    static const char*   inputTypeToStr(TrackInputType t) noexcept;
    static TrackInputType inputTypeFromStr(const juce::String& s) noexcept;

    // Convert AutomationCurve enum ↔ string constant
    static const char*       curveToStr(AutomationCurve c) noexcept;
    static AutomationCurve   curveFromStr(const juce::String& s) noexcept;

    // ISO 8601 time helpers
    static juce::String timeToIso(const juce::Time& t);
    static juce::Time   isoToTime(const juce::String& s);

private:
    // Produce indented JSON via juce::JSON with formatting
    static juce::String prettyPrint(const juce::var& v);
};

} // namespace y2k::engine
