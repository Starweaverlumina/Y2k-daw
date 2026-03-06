// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/session/session_serializer.cpp  — v10
// ═══════════════════════════════════════════════════════════════════
#include "session_serializer.h"
#include "../mixer/track_model.h"
#include <juce_core/juce_core.h>

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  Helpers — enums ↔ strings
// ─────────────────────────────────────────────────────────────────

const char* SessionSerializer::tapToStr(MeterTapPoint tap) noexcept {
    return tap == MeterTapPoint::PostInsert ? kTapPostInsert : kTapPostFader;
}
MeterTapPoint SessionSerializer::tapFromStr(const juce::String& s) noexcept {
    return s == kTapPostInsert ? MeterTapPoint::PostInsert : MeterTapPoint::PostFader;
}

const char* SessionSerializer::inputTypeToStr(TrackInputType t) noexcept {
    switch (t) {
        case TrackInputType::Instrument:  return kInputInstrument;
        case TrackInputType::AudioIn:     return kInputAudioIn;
        case TrackInputType::BusReceive:  return kInputBusReceive;
        default:                          return kInputNone;
    }
}
TrackInputType SessionSerializer::inputTypeFromStr(const juce::String& s) noexcept {
    if (s == kInputInstrument)  return TrackInputType::Instrument;
    if (s == kInputAudioIn)     return TrackInputType::AudioIn;
    if (s == kInputBusReceive)  return TrackInputType::BusReceive;
    return TrackInputType::None;
}

const char* SessionSerializer::curveToStr(AutomationCurve c) noexcept {
    switch (c) {
        case AutomationCurve::Exponential: return kCurveExp;
        case AutomationCurve::Stepped:     return kCurveStepped;
        default:                           return kCurveLinear;
    }
}
AutomationCurve SessionSerializer::curveFromStr(const juce::String& s) noexcept {
    if (s == kCurveExp)     return AutomationCurve::Exponential;
    if (s == kCurveStepped) return AutomationCurve::Stepped;
    return AutomationCurve::Linear;
}

juce::String SessionSerializer::timeToIso(const juce::Time& t) {
    return t.toISO8601(true);
}
juce::Time SessionSerializer::isoToTime(const juce::String& s) {
    return juce::Time::fromISO8601(s);
}

juce::String SessionSerializer::prettyPrint(const juce::var& v) {
    return juce::JSON::toString(v, true);
}

// ─────────────────────────────────────────────────────────────────
//  InsertSlot ↔ JSON
// ─────────────────────────────────────────────────────────────────

juce::var SessionSerializer::insertToJson(const InsertSlot& ins) {
    auto obj = new juce::DynamicObject();
    obj->setProperty(kInsertTypeId,   ins.typeId);
    obj->setProperty(kInsertBypassed, ins.bypassed);
    juce::Array<juce::var> params;
    for (auto& [idx, val] : ins.params) {
        juce::Array<juce::var> pair;
        pair.add(idx); pair.add(double(val));
        params.add(pair);
    }
    obj->setProperty(kInsertParams, params);
    return juce::var(obj);
}

bool SessionSerializer::insertFromJson(const juce::var& json, InsertSlot& out) {
    if (!json.isObject()) return false;
    out.typeId    = json[kInsertTypeId].toString();
    out.bypassed  = static_cast<bool>(json[kInsertBypassed]);
    const auto& params = *json[kInsertParams].getArray();
    out.params.clear();
    for (const auto& p : params) {
        if (p.isArray() && p.size() >= 2)
            out.params.push_back({ int(p[0]), float(double(p[1])) });
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  SendSlot ↔ JSON
// ─────────────────────────────────────────────────────────────────

juce::var SessionSerializer::sendToJson(const SendSlot& s) {
    auto obj = new juce::DynamicObject();
    obj->setProperty(kSendTargetBusId, s.targetBusId.toString());
    obj->setProperty(kSendLevel,       double(s.level));
    obj->setProperty(kSendPreFader,    s.preFader);
    obj->setProperty(kSendEnabled,     s.enabled);
    return juce::var(obj);
}

bool SessionSerializer::sendFromJson(const juce::var& json, SendSlot& out) {
    if (!json.isObject()) return false;
    out.targetBusId = juce::Uuid(json[kSendTargetBusId].toString());
    out.level       = float(double(json[kSendLevel]));
    out.preFader    = static_cast<bool>(json[kSendPreFader]);
    out.enabled     = json[kSendEnabled].isVoid() ? true
                    : static_cast<bool>(json[kSendEnabled]);
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  AutomationLane ↔ JSON
// ─────────────────────────────────────────────────────────────────

juce::var SessionSerializer::autoLanesToJson(const std::vector<AutomationLane>& lanes) {
    juce::Array<juce::var> arr;
    for (const auto& lane : lanes) {
        auto laneObj = new juce::DynamicObject();
        laneObj->setProperty(kAutoParamIdx, lane.parameterIndex);
        juce::Array<juce::var> pts;
        for (const auto& pt : lane.points) {
            auto ptObj = new juce::DynamicObject();
            ptObj->setProperty(kAutoBeat,  pt.beat);
            ptObj->setProperty(kAutoValue, double(pt.value));
            ptObj->setProperty(kAutoCurve, curveToStr(lane.curve));
            pts.add(juce::var(ptObj));
        }
        laneObj->setProperty(kAutoPoints, pts);
        arr.add(juce::var(laneObj));
    }
    return arr;
}

bool SessionSerializer::autoLanesFromJson(const juce::var& json,
                                           std::vector<AutomationLane>& out) {
    out.clear();
    if (!json.isArray()) return true; // empty is OK
    for (const auto& laneJson : *json.getArray()) {
        AutomationLane lane;
        lane.parameterIndex = int(laneJson[kAutoParamIdx]);
        const auto* pts = laneJson[kAutoPoints].getArray();
        if (pts) {
            for (const auto& ptJson : *pts) {
                AutomationPoint pt;
                pt.beat  = double(ptJson[kAutoBeat]);
                pt.value = float(double(ptJson[kAutoValue]));
                lane.curve = curveFromStr(ptJson[kAutoCurve].toString());
                lane.points.push_back(pt);
            }
        }
        out.push_back(lane);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  TrackModel ↔ JSON
// ─────────────────────────────────────────────────────────────────

juce::var SessionSerializer::trackToJson(const TrackModel& t) {
    auto obj = new juce::DynamicObject();
    obj->setProperty(kId,           t.id.toString());
    obj->setProperty(kName,         t.name);
    obj->setProperty(kInputType,    inputTypeToStr(t.inputType));
    obj->setProperty(kInstrumentId, t.instrumentId);

    juce::Array<juce::var> chans;
    chans.add(t.audioInCh[0]); chans.add(t.audioInCh[1]);
    obj->setProperty(kAudioInChannels, chans);

    juce::Array<juce::var> inserts;
    for (const auto& ins : t.inserts) inserts.add(insertToJson(ins));
    obj->setProperty(kInserts, inserts);

    juce::Array<juce::var> sends;
    for (const auto& s : t.sends) sends.add(sendToJson(s));
    obj->setProperty(kSends, sends);

    obj->setProperty(kVolume,  double(t.volume));
    obj->setProperty(kPan,     double(t.pan));
    obj->setProperty(kMuted,   t.muted);
    obj->setProperty(kSoloed,  t.soloed);

    if (t.outputBusId.has_value())
        obj->setProperty(kOutputBusId, t.outputBusId->toString());
    else
        obj->setProperty(kOutputBusId, juce::var());

    obj->setProperty(kMeterTap,  tapToStr(t.meterTap));
    obj->setProperty(kAutoLanes, autoLanesToJson(t.automationLanes));
    return juce::var(obj);
}

bool SessionSerializer::trackFromJson(const juce::var& json, TrackModel& out) {
    if (!json.isObject()) return false;
    out.id           = juce::Uuid(json[kId].toString());
    out.name         = json[kName].toString();
    out.inputType    = inputTypeFromStr(json[kInputType].toString());
    out.instrumentId = json[kInstrumentId].toString();

    const auto* chans = json[kAudioInChannels].getArray();
    if (chans && chans->size() >= 2) {
        out.audioInCh[0] = int((*chans)[0]);
        out.audioInCh[1] = int((*chans)[1]);
    }

    out.inserts.clear();
    if (const auto* insArr = json[kInserts].getArray())
        for (const auto& ins : *insArr) {
            InsertSlot slot; insertFromJson(ins, slot);
            out.inserts.push_back(slot);
        }

    out.sends.clear();
    if (const auto* sendsArr = json[kSends].getArray())
        for (const auto& s : *sendsArr) {
            SendSlot slot; sendFromJson(s, slot);
            out.sends.push_back(slot);
        }

    out.volume  = float(double(json[kVolume]));
    out.pan     = float(double(json[kPan]));
    out.muted   = static_cast<bool>(json[kMuted]);
    out.soloed  = static_cast<bool>(json[kSoloed]);

    const auto busStr = json[kOutputBusId].toString();
    if (busStr.isNotEmpty() && busStr != "null" && busStr != "undefined")
        out.outputBusId = juce::Uuid(busStr);
    else
        out.outputBusId.reset();

    out.meterTap = tapFromStr(json[kMeterTap].toString());
    autoLanesFromJson(json[kAutoLanes], out.automationLanes);

    // Clear compiled IDs — never restored from disk
    out.compiled = {};
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  BusModel ↔ JSON
// ─────────────────────────────────────────────────────────────────

juce::var SessionSerializer::busToJson(const BusModel& b) {
    auto obj = new juce::DynamicObject();
    obj->setProperty(kId,        b.id.toString());
    obj->setProperty(kName,      b.name);
    obj->setProperty(kIsMaster,  b.isMaster);

    juce::Array<juce::var> inserts;
    for (const auto& ins : b.inserts) inserts.add(insertToJson(ins));
    obj->setProperty(kInserts, inserts);

    obj->setProperty(kVolume,  double(b.volume));
    obj->setProperty(kMuted,   b.muted);
    obj->setProperty(kMeterTap, tapToStr(b.meterTap));
    obj->setProperty(kAutoLanes, autoLanesToJson(b.automationLanes));
    return juce::var(obj);
}

bool SessionSerializer::busFromJson(const juce::var& json, BusModel& out) {
    if (!json.isObject()) return false;
    out.id        = juce::Uuid(json[kId].toString());
    out.name      = json[kName].toString();
    out.isMaster  = static_cast<bool>(json[kIsMaster]);
    out.volume    = float(double(json[kVolume]));
    out.muted     = static_cast<bool>(json[kMuted]);
    out.meterTap  = tapFromStr(json[kMeterTap].toString());

    out.inserts.clear();
    if (const auto* insArr = json[kInserts].getArray())
        for (const auto& ins : *insArr) {
            InsertSlot slot; insertFromJson(ins, slot);
            out.inserts.push_back(slot);
        }
    autoLanesFromJson(json[kAutoLanes], out.automationLanes);
    out.compiled = {};
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  ProjectRouting ↔ JSON
// ─────────────────────────────────────────────────────────────────

juce::var SessionSerializer::routingToJson(const ProjectRouting& r) {
    auto obj = new juce::DynamicObject();

    juce::Array<juce::var> tracks;
    for (const auto& t : r.tracks) tracks.add(trackToJson(t));
    obj->setProperty(kRoutingTracks, tracks);

    juce::Array<juce::var> buses;
    for (const auto& b : r.buses) buses.add(busToJson(b));
    obj->setProperty(kRoutingBuses, buses);

    obj->setProperty(kRoutingMaster, busToJson(r.masterBus));
    return juce::var(obj);
}

bool SessionSerializer::routingFromJson(const juce::var& json, ProjectRouting& out) {
    if (!json.isObject()) return false;

    out.tracks.clear();
    if (const auto* arr = json[kRoutingTracks].getArray())
        for (const auto& t : *arr) {
            TrackModel tm; trackFromJson(t, tm);
            out.tracks.push_back(std::move(tm));
        }

    out.buses.clear();
    if (const auto* arr = json[kRoutingBuses].getArray())
        for (const auto& b : *arr) {
            BusModel bm; busFromJson(b, bm);
            out.buses.push_back(std::move(bm));
        }

    busFromJson(json[kRoutingMaster], out.masterBus);
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  Full session ↔ JSON
// ─────────────────────────────────────────────────────────────────

juce::String SessionSerializer::toJsonString(
    const ProjectRouting&         routing,
    const SessionMetaData&        meta,
    const SessionTransportData&   transport,
    const juce::Array<juce::var>& markers)
{
    auto root = new juce::DynamicObject();

    // schema
    auto schemaObj = new juce::DynamicObject();
    schemaObj->setProperty(kSchemaVersion_, kSchemaVersion);
    schemaObj->setProperty(kSchemaDaw,      kDawName);
    schemaObj->setProperty(kSchemaDawVer,   meta.dawVersion);
    root->setProperty(kKeySchema, juce::var(schemaObj));

    // meta
    auto metaObj = new juce::DynamicObject();
    metaObj->setProperty(kMetaId,       meta.id.isEmpty() ? juce::Uuid().toString() : meta.id);
    metaObj->setProperty(kMetaName,     meta.name);
    metaObj->setProperty(kMetaAuthor,   meta.author);
    metaObj->setProperty(kMetaCreated,  meta.created.isEmpty()
                                          ? timeToIso(juce::Time::getCurrentTime())
                                          : meta.created);
    metaObj->setProperty(kMetaModified, timeToIso(juce::Time::getCurrentTime()));
    root->setProperty(kKeyMeta, juce::var(metaObj));

    // transport
    auto transObj = new juce::DynamicObject();
    transObj->setProperty(kTransBpm,          transport.bpm);
    transObj->setProperty(kTransTimeSigNum,    transport.timeSigNum);
    transObj->setProperty(kTransTimeSigDen,    transport.timeSigDen);
    transObj->setProperty(kTransLoopStart,     transport.loopStart);
    transObj->setProperty(kTransLoopEnd,       transport.loopEnd);
    transObj->setProperty(kTransLoopEnabled,   transport.loopEnabled);
    transObj->setProperty(kTransTotalBeats,    transport.totalBeats);
    root->setProperty(kKeyTransport, juce::var(transObj));

    // routing
    root->setProperty(kKeyRouting, routingToJson(routing));

    // markers
    root->setProperty(kKeyMarkers, markers.isEmpty() ? juce::var(juce::Array<juce::var>{})
                                                      : juce::var(markers));

    return prettyPrint(juce::var(root));
}

bool SessionSerializer::fromJsonString(const juce::String& jsonStr, SessionData& out) {
    juce::var parsed;
    auto result = juce::JSON::parse(jsonStr, parsed);
    if (!result.wasOk() || !parsed.isObject()) {
        juce::Logger::writeToLog("[SessionSerializer] JSON parse failed: " + result.getErrorMessage());
        return false;
    }

    // schema
    const auto& schema = parsed[kKeySchema];
    out.meta.schemaVersion = schema[kSchemaVersion_].toString();
    out.meta.dawVersion    = schema[kSchemaDawVer].toString();

    // meta
    const auto& meta = parsed[kKeyMeta];
    out.meta.id       = meta[kMetaId].toString();
    out.meta.name     = meta[kMetaName].toString();
    out.meta.author   = meta[kMetaAuthor].toString();
    out.meta.created  = meta[kMetaCreated].toString();
    out.meta.modified = meta[kMetaModified].toString();

    // transport
    const auto& tr = parsed[kKeyTransport];
    out.transport.bpm         = double(tr[kTransBpm]);
    out.transport.timeSigNum  = int(tr[kTransTimeSigNum]);
    out.transport.timeSigDen  = int(tr[kTransTimeSigDen]);
    out.transport.loopStart   = double(tr[kTransLoopStart]);
    out.transport.loopEnd     = double(tr[kTransLoopEnd]);
    out.transport.loopEnabled = static_cast<bool>(tr[kTransLoopEnabled]);
    out.transport.totalBeats  = double(tr[kTransTotalBeats]);

    out.routingJson = parsed[kKeyRouting];
    out.markersJson = parsed[kKeyMarkers];
    return true;
}

} // namespace y2k::engine
