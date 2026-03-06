// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/session/session_validator.cpp  — v10
// ═══════════════════════════════════════════════════════════════════
#include "session_validator.h"
#include "session_serializer.h"

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  ValidationReport helpers
// ─────────────────────────────────────────────────────────────────
juce::Result ValidationReport::toResult() const {
    if (!hasErrors()) return juce::Result::ok();
    return juce::Result::fail(summary());
}

juce::String ValidationReport::summary() const {
    juce::String s;
    for (const auto& e : errors)
        s += "[ERROR] " + e.field + ": " + e.message + "\n";
    for (const auto& w : warnings)
        s += "[WARN]  " + w.field + ": " + w.message + "\n";
    return s;
}

// ─────────────────────────────────────────────────────────────────
//  Migration registry
//  Each entry migrates one schema version to the next.
//  Add new entries here as schema evolves. Never modify existing entries.
// ─────────────────────────────────────────────────────────────────
const std::vector<SessionMigration>& SessionValidator::migrations() {
    static const std::vector<SessionMigration> kMigrations = {
        // ── Migration 1: v1.0 → v1.1 ─────────────────────────────────
        // Adds meterTap="postFader" to all tracks and buses in routingJson
        // that are missing the field (new metering tap point feature).
        {
            "1.0", "1.1",
            "Add meterTap=postFader to all tracks and buses",
            [](juce::var& root) -> bool
            {
                auto& routing = root[kKeyRouting];
                if (!routing.isObject()) return false;

                // Patch all tracks
                if (auto* tracks = routing[kRoutingTracks].getArray()) {
                    for (auto& t : *tracks) {
                        auto* obj = t.getDynamicObject();
                        if (obj && t[kMeterTap].isVoid())
                            obj->setProperty(kMeterTap, juce::String(kTapPostFader));
                    }
                }

                // Patch all buses
                if (auto* buses = routing[kRoutingBuses].getArray()) {
                    for (auto& b : *buses) {
                        auto* obj = b.getDynamicObject();
                        if (obj && b[kMeterTap].isVoid())
                            obj->setProperty(kMeterTap, juce::String(kTapPostFader));
                    }
                }

                // Patch master bus
                {
                    auto& master = routing[kRoutingMaster];
                    auto* obj = master.getDynamicObject();
                    if (obj && master[kMeterTap].isVoid())
                        obj->setProperty(kMeterTap, juce::String(kTapPostFader));
                }

                // Bump version in schema block
                if (auto* schemaObj = root[kKeySchema].getDynamicObject())
                    schemaObj->setProperty(kSchemaVersion_, juce::String("1.1"));

                return true;
            }
        },

        // ── Migration 2: v1.1 → v2.0 ─────────────────────────────────
        // Adds inputType="instrument" to tracks that lack it.
        // Adds empty sends[] and inserts[] arrays to tracks that lack them.
        // (v2.0 introduced explicit inputType and unified insert/send model.)
        {
            "1.1", "2.0",
            "Add inputType=instrument and empty sends/inserts to tracks",
            [](juce::var& root) -> bool
            {
                auto& routing = root[kKeyRouting];
                if (!routing.isObject()) return false;

                if (auto* tracks = routing[kRoutingTracks].getArray()) {
                    for (auto& t : *tracks) {
                        auto* obj = t.getDynamicObject();
                        if (!obj) continue;

                        // inputType field
                        if (t[kInputType].isVoid())
                            obj->setProperty(kInputType, juce::String(kInputInstrument));

                        // sends array
                        if (t[kSends].isVoid())
                            obj->setProperty(kSends, juce::Array<juce::var>{});

                        // inserts array
                        if (t[kInserts].isVoid())
                            obj->setProperty(kInserts, juce::Array<juce::var>{});
                    }
                }

                // Bump version in schema block
                if (auto* schemaObj = root[kKeySchema].getDynamicObject())
                    schemaObj->setProperty(kSchemaVersion_, juce::String(kSchemaVersion));

                return true;
            }
        },
    };
    return kMigrations;
}

// ─────────────────────────────────────────────────────────────────
//  checkVersion
// ─────────────────────────────────────────────────────────────────
juce::Result SessionValidator::checkVersion(const SessionData& data) {
    const auto& ver = data.meta.schemaVersion;
    if (ver.isEmpty())
        return juce::Result::fail("Session has no schema version field.");

    // Simple lexicographic comparison works for X.Y version strings
    if (ver > juce::String(kSchemaVersion))
        return juce::Result::fail(
            "Session was created with schema v" + ver +
            ", but this DAW only reads up to v" + kSchemaVersion +
            ". Update your DAW to open this project.");

    if (ver < juce::String(kMinReadableSchema))
        return juce::Result::fail(
            "Session schema v" + ver + " is too old to migrate automatically.");

    return juce::Result::ok();
}

// ─────────────────────────────────────────────────────────────────
//  migrate
// ─────────────────────────────────────────────────────────────────
juce::Result SessionValidator::migrate(SessionData& data) {
    juce::String current = data.meta.schemaVersion;
    const juce::String target = kSchemaVersion;
    if (current == target) return juce::Result::ok();

    // Build a temporary root var that contains routingJson and meta
    // so migrations can mutate both
    auto rootObj = new juce::DynamicObject();
    auto schemaObj = new juce::DynamicObject();
    schemaObj->setProperty(kSchemaVersion_, current);
    rootObj->setProperty(kKeySchema,  juce::var(schemaObj));
    rootObj->setProperty(kKeyRouting, data.routingJson);
    juce::var root(rootObj);

    for (const auto& mig : migrations()) {
        if (current != mig.fromVersion) continue;
        juce::Logger::writeToLog("[SessionMigrator] Migrating " +
                                  mig.fromVersion + " → " + mig.toVersion +
                                  ": " + mig.description);
        if (!mig.migrate(root))
            return juce::Result::fail("Migration " + mig.fromVersion +
                                       "→" + mig.toVersion + " failed.");
        current = mig.toVersion;
        if (current == target) break;
    }

    if (current != target)
        return juce::Result::fail("No migration path from v" + data.meta.schemaVersion +
                                   " to v" + target);

    // Extract mutated routing back into SessionData
    data.routingJson = root[kKeyRouting];
    data.meta.schemaVersion = current;
    return juce::Result::ok();
}

// ─────────────────────────────────────────────────────────────────
//  validate (raw JSON)
// ─────────────────────────────────────────────────────────────────
void SessionValidator::validateTransport(const SessionTransportData& t,
                                          ValidationReport& report)
{
    if (t.bpm < 20.0 || t.bpm > 999.0)
        report.errors.push_back({ValidationError::Level::Error,
            "transport.bpm",
            "BPM must be in range [20, 999], got " + juce::String(t.bpm)});
    if (t.timeSigNum < 1 || t.timeSigNum > 32)
        report.errors.push_back({ValidationError::Level::Error,
            "transport.timeSigNum",
            "Time sig numerator must be 1–32, got " + juce::String(t.timeSigNum)});
    if (t.timeSigDen != 2 && t.timeSigDen != 4 && t.timeSigDen != 8 && t.timeSigDen != 16)
        report.errors.push_back({ValidationError::Level::Error,
            "transport.timeSigDen",
            "Time sig denominator must be 2/4/8/16, got " + juce::String(t.timeSigDen)});
    if (t.loopEnd <= t.loopStart && t.loopEnabled)
        report.errors.push_back({ValidationError::Level::Warning,
            "transport.loop",
            "Loop end ≤ loop start while loop is enabled"});
}

void SessionValidator::validateSendJson(const juce::var& send,
                                         const juce::String& trackName,
                                         int sendIdx,
                                         const juce::StringArray& busIds,
                                         ValidationReport& report)
{
    const juce::String field = "tracks[" + trackName + "].sends[" + juce::String(sendIdx) + "]";
    const auto targetId = send[kSendTargetBusId].toString();
    if (targetId.isEmpty() || targetId == "null")
        report.errors.push_back({ValidationError::Level::Error, field,
            "send.targetBusId is empty"});
    else if (!busIds.contains(targetId))
        report.errors.push_back({ValidationError::Level::Error, field,
            "send.targetBusId '" + targetId + "' references nonexistent bus"});
    const float level = float(double(send[kSendLevel]));
    if (level < 0.f || level > 1.f)
        report.warnings.push_back({ValidationError::Level::Warning, field + ".level",
            "send level " + juce::String(level, 3) + " outside [0,1]"});
}

void SessionValidator::validateTrackJson(const juce::var& track, int idx,
                                          const juce::StringArray& busIds,
                                          ValidationReport& report)
{
    const juce::String field = "tracks[" + juce::String(idx) + "]";
    const auto id   = track[kId].toString();
    const auto name = track[kName].toString();
    if (id.isEmpty())
        report.errors.push_back({ValidationError::Level::Error, field + ".id",
            "track has empty ID"});
    if (name.isEmpty())
        report.warnings.push_back({ValidationError::Level::Warning, field + ".name",
            "track has empty name"});

    const auto outBusStr = track[kOutputBusId].toString();
    if (!outBusStr.isEmpty() && outBusStr != "null" && outBusStr != "undefined")
        if (!busIds.contains(outBusStr))
            report.errors.push_back({ValidationError::Level::Error,
                field + ".outputBusId",
                "outputBusId '" + outBusStr + "' references nonexistent bus"});

    if (const auto* sends = track[kSends].getArray()) {
        int si = 0;
        for (const auto& s : *sends)
            validateSendJson(s, name.isEmpty() ? juce::String(idx) : name, si++, busIds, report);
    }
}

void SessionValidator::validateBusJson(const juce::var& bus, int idx,
                                        ValidationReport& report)
{
    const juce::String field = "buses[" + juce::String(idx) + "]";
    if (bus[kId].toString().isEmpty())
        report.errors.push_back({ValidationError::Level::Error, field + ".id",
            "bus has empty ID"});
    if (bus[kName].toString().isEmpty())
        report.warnings.push_back({ValidationError::Level::Warning, field + ".name",
            "bus has empty name"});
}

void SessionValidator::validateRoutingJson(const juce::var& routing,
                                            ValidationReport& report)
{
    // Collect all bus IDs first (master + non-master)
    juce::StringArray busIds;
    if (const auto* buses = routing[kRoutingBuses].getArray())
        for (const auto& b : *buses)
            if (!b[kId].toString().isEmpty())
                busIds.add(b[kId].toString());

    const auto masterId = routing[kRoutingMaster][kId].toString();
    if (!masterId.isEmpty()) busIds.add(masterId);
    else report.errors.push_back({ValidationError::Level::Error,
         "routing.master.id", "masterBus has null/empty ID"});

    // Check duplicate bus IDs
    {
        juce::StringArray seen;
        for (const auto& id : busIds) {
            if (seen.contains(id))
                report.errors.push_back({ValidationError::Level::Error,
                    "routing.buses", "Duplicate bus ID: " + id});
            seen.add(id);
        }
    }

    // Validate buses
    if (const auto* buses = routing[kRoutingBuses].getArray()) {
        int i = 0;
        for (const auto& b : *buses) validateBusJson(b, i++, report);
    }

    // Validate tracks
    juce::StringArray trackIds;
    if (const auto* tracks = routing[kRoutingTracks].getArray()) {
        int i = 0;
        for (const auto& t : *tracks) {
            const auto id = t[kId].toString();
            if (trackIds.contains(id))
                report.errors.push_back({ValidationError::Level::Error,
                    "routing.tracks", "Duplicate track ID: " + id});
            trackIds.add(id);
            validateTrackJson(t, i++, busIds, report);
        }
    }
}

ValidationReport SessionValidator::validate(const SessionData& data) {
    ValidationReport report;
    validateTransport(data.transport, report);
    if (data.routingJson.isObject())
        validateRoutingJson(data.routingJson, report);
    else
        report.errors.push_back({ValidationError::Level::Error,
            "routing", "routing section is missing or not an object"});
    return report;
}

// ─────────────────────────────────────────────────────────────────
//  validateRouting (post-deserialization, takes ProjectRouting)
// ─────────────────────────────────────────────────────────────────
ValidationReport SessionValidator::validateRouting(const ProjectRouting& r) {
    ValidationReport report;

    if (r.masterBus.id.isNull())
        report.errors.push_back({ValidationError::Level::Error,
            "routing.master.id", "masterBus has null ID"});

    // Collect bus IDs
    juce::StringArray busIds;
    busIds.add(r.masterBus.id.toString());
    for (const auto& b : r.buses) {
        if (b.id.isNull()) {
            report.errors.push_back({ValidationError::Level::Error,
                "routing.buses", "bus has null ID"});
            continue;
        }
        if (busIds.contains(b.id.toString()))
            report.errors.push_back({ValidationError::Level::Error,
                "routing.buses", "Duplicate bus ID: " + b.id.toString()});
        busIds.add(b.id.toString());
    }

    // Validate tracks
    juce::StringArray trackIds;
    int ti = 0;
    for (const auto& t : r.tracks) {
        const juce::String field = "tracks[" + juce::String(ti++) + "]";
        if (t.id.isNull()) {
            report.errors.push_back({ValidationError::Level::Error,
                field + ".id", "null ID"});
        } else {
            if (trackIds.contains(t.id.toString()))
                report.errors.push_back({ValidationError::Level::Error,
                    field + ".id", "Duplicate ID: " + t.id.toString()});
            trackIds.add(t.id.toString());
        }
        if (t.outputBusId.has_value() && !busIds.contains(t.outputBusId->toString()))
            report.errors.push_back({ValidationError::Level::Error,
                field + ".outputBusId",
                "References nonexistent bus: " + t.outputBusId->toString()});
        int si = 0;
        for (const auto& s : t.sends) {
            if (!busIds.contains(s.targetBusId.toString()))
                report.errors.push_back({ValidationError::Level::Error,
                    field + ".sends[" + juce::String(si) + "].targetBusId",
                    "References nonexistent bus: " + s.targetBusId.toString()});
            ++si;
        }
    }
    return report;
}

// ─────────────────────────────────────────────────────────────────
//  validateAndMigrate — main pipeline
// ─────────────────────────────────────────────────────────────────
juce::Result SessionValidator::validateAndMigrate(SessionData& data) {
    auto vr = checkVersion(data);
    if (!vr.wasOk()) return vr;

    auto mr = migrate(data);
    if (!mr.wasOk()) return mr;

    const auto report = validate(data);
    return report.toResult();
}

} // namespace y2k::engine
