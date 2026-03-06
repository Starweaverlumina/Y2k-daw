// session_schema.cpp — v10: see session_schema.h for design notes
#include "session_schema.h"
#if JUCE_WINDOWS
  #include <windows.h>
#else
  #include <unistd.h>
  #include <fcntl.h>
#endif

namespace y2k::engine {

juce::String SessionSchema::schemaVersion(const juce::ValueTree& root) noexcept {
    return root.getProperty("schemaVersion", "1.0").toString();
}

bool SessionSchema::isCompatible(const juce::ValueTree& root) noexcept {
    const auto ver = schemaVersion(root);
    return ver == "1.0" || ver == "2.0";
}

juce::String SessionSchema::migrate(juce::ValueTree& root) {
    const auto ver = schemaVersion(root);
    if (ver == SCHEMA_VERSION) return {};

    juce::String log;
    root.setProperty("schemaVersion", SCHEMA_VERSION, nullptr);
    log += "  v1.0 -> v2.0\n";

    if (!root.getChildWithName("Routing").isValid()) {
        juce::ValueTree rv("Routing");
        juce::ValueTree mv("MasterBus");
        mv.setProperty("id",     juce::Uuid().toString(), nullptr);
        mv.setProperty("name",   "Master",                nullptr);
        mv.setProperty("volume", 1.0f,                    nullptr);
        rv.appendChild(mv, nullptr);
        rv.appendChild(juce::ValueTree("Buses"),        nullptr);
        rv.appendChild(juce::ValueTree("TrackRouting"), nullptr);
        root.appendChild(rv, nullptr);
        log += "    + Routing subtree\n";
    }

    auto tracksVt = root.getChildWithName("Tracks");
    for (int i = 0; i < tracksVt.getNumChildren(); ++i) {
        auto t = tracksVt.getChild(i);
        if (!t.getChildWithName("Inserts").isValid())
            t.appendChild(juce::ValueTree("Inserts"), nullptr);
        if (!t.getChildWithName("Sends").isValid())
            t.appendChild(juce::ValueTree("Sends"), nullptr);
    }
    log += "    + Insert/Send subtrees\n";
    return log;
}

// ── Insert / Send helpers ──────────────────────────────────────────
juce::ValueTree SessionSchema::serialiseInserts(const std::vector<InsertSlot>& ins) {
    juce::ValueTree vt("Inserts");
    for (const auto& s : ins) {
        juce::ValueTree sv("Insert");
        sv.setProperty("typeId",   s.typeId,   nullptr);
        sv.setProperty("bypassed", s.bypassed, nullptr);
        juce::ValueTree pv("Params");
        for (const auto& [idx, val] : s.params) {
            juce::ValueTree p("P");
            p.setProperty("i", idx, nullptr);
            p.setProperty("v", val, nullptr);
            pv.appendChild(p, nullptr);
        }
        sv.appendChild(pv, nullptr);
        vt.appendChild(sv, nullptr);
    }
    return vt;
}

juce::ValueTree SessionSchema::serialiseSends(const std::vector<SendSlot>& sends) {
    juce::ValueTree vt("Sends");
    for (const auto& s : sends) {
        juce::ValueTree sv("Send");
        sv.setProperty("targetBusId", s.targetBusId.toString(), nullptr);
        sv.setProperty("level",       s.level,                  nullptr);
        sv.setProperty("preFader",    s.preFader,               nullptr);
        sv.setProperty("enabled",     s.enabled,                nullptr);
        vt.appendChild(sv, nullptr);
    }
    return vt;
}

std::vector<InsertSlot> SessionSchema::deserialiseInserts(const juce::ValueTree& vt) {
    std::vector<InsertSlot> result;
    for (int i = 0; i < vt.getNumChildren(); ++i) {
        auto sv = vt.getChild(i);
        InsertSlot slot;
        slot.typeId   = sv.getProperty("typeId",   "").toString();
        slot.bypassed = static_cast<bool>(sv.getProperty("bypassed", false));
        auto pv = sv.getChildWithName("Params");
        for (int j = 0; j < pv.getNumChildren(); ++j) {
            auto p = pv.getChild(j);
            slot.params.push_back({ static_cast<int>(p.getProperty("i", 0)),
                                    static_cast<float>(p.getProperty("v", 0.f)) });
        }
        result.push_back(std::move(slot));
    }
    return result;
}

std::vector<SendSlot> SessionSchema::deserialiseSends(const juce::ValueTree& vt) {
    std::vector<SendSlot> result;
    for (int i = 0; i < vt.getNumChildren(); ++i) {
        auto sv = vt.getChild(i);
        SendSlot s;
        s.targetBusId = juce::Uuid(sv.getProperty("targetBusId", "").toString());
        s.level       = static_cast<float>(sv.getProperty("level",    1.f));
        s.preFader    = static_cast<bool> (sv.getProperty("preFader", false));
        s.enabled     = static_cast<bool> (sv.getProperty("enabled",  true));
        result.push_back(s);
    }
    return result;
}

// ── Top-level serialise ────────────────────────────────────────────
juce::ValueTree SessionSchema::serialiseTimeline(const Y2KSession& s) {
    juce::ValueTree vt("Timeline");
    vt.setProperty("tempo",      s.timeline.tempo,      nullptr);
    vt.setProperty("timeSigNum", s.timeline.timeSigNum, nullptr);
    vt.setProperty("timeSigDen", s.timeline.timeSigDen, nullptr);
    vt.setProperty("sampleRate", s.timeline.sampleRate, nullptr);
    vt.setProperty("bitDepth",   s.timeline.bitDepth,   nullptr);
    vt.setProperty("totalBeats", s.timeline.totalBeats, nullptr);
    vt.setProperty("loopStart",  s.loopStartBeat,       nullptr);
    vt.setProperty("loopEnd",    s.loopEndBeat,         nullptr);
    vt.setProperty("loopOn",     s.loopEnabled,         nullptr);
    return vt;
}

juce::ValueTree SessionSchema::serialiseTracks(const Y2KSession& s) {
    juce::ValueTree vt("Tracks");
    for (const auto& t : s.tracks) {
        auto tv = t->toValueTree();
        if (!tv.getChildWithName("Inserts").isValid())
            tv.appendChild(juce::ValueTree("Inserts"), nullptr);
        if (!tv.getChildWithName("Sends").isValid())
            tv.appendChild(juce::ValueTree("Sends"), nullptr);
        vt.appendChild(tv, nullptr);
    }
    return vt;
}

juce::ValueTree SessionSchema::serialiseRouting(const ProjectRouting& r) {
    juce::ValueTree vt("Routing");

    juce::ValueTree mv("MasterBus");
    mv.setProperty("id",     r.masterBus.id.toString(), nullptr);
    mv.setProperty("name",   r.masterBus.name,          nullptr);
    mv.setProperty("volume", r.masterBus.volume,        nullptr);
    mv.setProperty("muted",  r.masterBus.muted,         nullptr);
    mv.appendChild(serialiseInserts(r.masterBus.inserts), nullptr);
    vt.appendChild(mv, nullptr);

    juce::ValueTree bv("Buses");
    for (const auto& b : r.buses) {
        juce::ValueTree bsv("Bus");
        bsv.setProperty("id",     b.id.toString(), nullptr);
        bsv.setProperty("name",   b.name,          nullptr);
        bsv.setProperty("volume", b.volume,        nullptr);
        bsv.setProperty("muted",  b.muted,         nullptr);
        bsv.appendChild(serialiseInserts(b.inserts), nullptr);
        bv.appendChild(bsv, nullptr);
    }
    vt.appendChild(bv, nullptr);

    juce::ValueTree trv("TrackRouting");
    for (const auto& t : r.tracks) {
        juce::ValueTree tv("TrackRoute");
        tv.setProperty("trackId",      t.id.toString(),              nullptr);
        tv.setProperty("name",         t.name,                       nullptr);
        tv.setProperty("volume",       t.volume,                     nullptr);
        tv.setProperty("pan",          t.pan,                        nullptr);
        tv.setProperty("muted",        t.muted,                      nullptr);
        tv.setProperty("soloed",       t.soloed,                     nullptr);
        tv.setProperty("instrumentId", t.instrumentId,               nullptr);
        tv.setProperty("inputType",    static_cast<int>(t.inputType),nullptr);
        if (t.outputBusId.has_value())
            tv.setProperty("outputBusId", t.outputBusId->toString(), nullptr);
        tv.appendChild(serialiseInserts(t.inserts), nullptr);
        tv.appendChild(serialiseSends  (t.sends),   nullptr);
        trv.appendChild(tv, nullptr);
    }
    vt.appendChild(trv, nullptr);
    return vt;
}

juce::ValueTree SessionSchema::serialiseMarkers(const Y2KSession& s) {
    juce::ValueTree vt("Markers");
    for (const auto& m : s.markers) {
        juce::ValueTree mv("Marker");
        mv.setProperty("beat",  m.beat,                    nullptr);
        mv.setProperty("label", m.label,                   nullptr);
        mv.setProperty("color", static_cast<int>(m.color), nullptr);
        vt.appendChild(mv, nullptr);
    }
    return vt;
}

juce::ValueTree SessionSchema::serialise(const Y2KSession& session,
                                          const ProjectRouting& routing) {
    juce::ValueTree root("Y2KSession");
    root.setProperty("schemaVersion", SCHEMA_VERSION,              nullptr);
    root.setProperty("id",            session.id.toString(),       nullptr);
    root.setProperty("projectName",   session.projectName,         nullptr);
    root.setProperty("author",        session.author,              nullptr);
    root.setProperty("dawVersion",    session.dawVersion,          nullptr);
    root.setProperty("savedAt", juce::Time::getCurrentTime().toISO8601(true), nullptr);
    root.appendChild(serialiseTimeline(session),  nullptr);
    root.appendChild(serialiseTracks  (session),  nullptr);
    root.appendChild(serialiseRouting (routing),  nullptr);
    root.appendChild(serialiseMarkers (session),  nullptr);
    return root;
}

// ── Deserialise helpers ────────────────────────────────────────────
void SessionSchema::deserialiseTimeline(const juce::ValueTree& vt, Y2KSession& s) {
    if (!vt.isValid()) return;
    s.timeline.tempo      = static_cast<double>(vt.getProperty("tempo",      120.0));
    s.timeline.timeSigNum = static_cast<int>   (vt.getProperty("timeSigNum", 4));
    s.timeline.timeSigDen = static_cast<int>   (vt.getProperty("timeSigDen", 4));
    s.timeline.sampleRate = static_cast<int>   (vt.getProperty("sampleRate", 44100));
    s.timeline.bitDepth   = static_cast<int>   (vt.getProperty("bitDepth",   24));
    s.timeline.totalBeats = static_cast<double>(vt.getProperty("totalBeats", 128.0));
    s.loopStartBeat       = static_cast<double>(vt.getProperty("loopStart",  0.0));
    s.loopEndBeat         = static_cast<double>(vt.getProperty("loopEnd",    8.0));
    s.loopEnabled         = static_cast<bool>  (vt.getProperty("loopOn",     false));
}

void SessionSchema::deserialiseTracks(const juce::ValueTree& vt, Y2KSession& s) {
    s.tracks.clear();
    for (int i = 0; i < vt.getNumChildren(); ++i) {
        auto t = Track::fromValueTree(vt.getChild(i));
        if (t) s.tracks.push_back(std::move(t));
    }
}

void SessionSchema::deserialiseRouting(const juce::ValueTree& rv, ProjectRouting& r) {
    if (!rv.isValid()) { r = ProjectRouting::createDefault(); return; }

    auto mv = rv.getChildWithName("MasterBus");
    if (mv.isValid()) {
        r.masterBus.id      = juce::Uuid(mv.getProperty("id",   "").toString());
        r.masterBus.name    = mv.getProperty("name",   "Master").toString();
        r.masterBus.volume  = static_cast<float>(mv.getProperty("volume", 1.f));
        r.masterBus.muted   = static_cast<bool> (mv.getProperty("muted",  false));
        r.masterBus.inserts = deserialiseInserts(mv.getChildWithName("Inserts"));
    }

    r.buses.clear();
    auto bv = rv.getChildWithName("Buses");
    for (int i = 0; i < bv.getNumChildren(); ++i) {
        auto bsv = bv.getChild(i);
        BusModel b;
        b.id      = juce::Uuid(bsv.getProperty("id",   "").toString());
        b.name    = bsv.getProperty("name", "Bus").toString();
        b.volume  = static_cast<float>(bsv.getProperty("volume", 1.f));
        b.muted   = static_cast<bool> (bsv.getProperty("muted",  false));
        b.inserts = deserialiseInserts(bsv.getChildWithName("Inserts"));
        r.buses.push_back(std::move(b));
    }

    r.tracks.clear();
    auto trv = rv.getChildWithName("TrackRouting");
    for (int i = 0; i < trv.getNumChildren(); ++i) {
        auto tv = trv.getChild(i);
        TrackModel t;
        t.id           = juce::Uuid(tv.getProperty("trackId", "").toString());
        t.name         = tv.getProperty("name",         "Track").toString();
        t.volume       = static_cast<float>(tv.getProperty("volume",  0.8f));
        t.pan          = static_cast<float>(tv.getProperty("pan",     0.f));
        t.muted        = static_cast<bool> (tv.getProperty("muted",   false));
        t.soloed       = static_cast<bool> (tv.getProperty("soloed",  false));
        t.instrumentId = tv.getProperty("instrumentId", "SineOsc").toString();
        t.inputType    = static_cast<TrackInputType>(
                            static_cast<int>(tv.getProperty("inputType", 0)));
        auto obid = tv.getProperty("outputBusId", "").toString();
        if (obid.isNotEmpty()) t.outputBusId = juce::Uuid(obid);
        t.inserts = deserialiseInserts(tv.getChildWithName("Inserts"));
        t.sends   = deserialiseSends  (tv.getChildWithName("Sends"));
        r.tracks.push_back(std::move(t));
    }
}

void SessionSchema::deserialiseMarkers(const juce::ValueTree& vt, Y2KSession& s) {
    s.markers.clear();
    for (int i = 0; i < vt.getNumChildren(); ++i) {
        auto mv = vt.getChild(i);
        ArrangementMarker m;
        m.beat  = static_cast<double>(mv.getProperty("beat",  0.0));
        m.label = mv.getProperty("label", "").toString();
        m.color = static_cast<TrackColor>(static_cast<int>(mv.getProperty("color", 0)));
        s.markers.push_back(m);
    }
}

SessionSchema::LoadResult SessionSchema::deserialise(const juce::ValueTree& rootIn) {
    LoadResult lr;
    if (!rootIn.isValid() || rootIn.getType().toString() != "Y2KSession") {
        lr.status = juce::Result::fail("Not a Y2KSession document");
        return lr;
    }
    juce::ValueTree root = rootIn.createCopy();
    if (schemaVersion(root) != SCHEMA_VERSION)
        lr.migrationLog = migrate(root);
    if (!isCompatible(root)) {
        lr.status = juce::Result::fail("Incompatible schema: " + schemaVersion(root));
        return lr;
    }
    lr.session = std::make_unique<Y2KSession>();
    auto& s = *lr.session;
    s.id          = juce::Uuid(root.getProperty("id", juce::Uuid().toString()).toString());
    s.projectName = root.getProperty("projectName", "Untitled").toString();
    s.author      = root.getProperty("author",       "").toString();
    s.dawVersion  = root.getProperty("dawVersion",   "1.0.0").toString();
    deserialiseTimeline(root.getChildWithName("Timeline"), s);
    deserialiseTracks  (root.getChildWithName("Tracks"),   s);
    deserialiseMarkers (root.getChildWithName("Markers"),  s);
    deserialiseRouting (root.getChildWithName("Routing"),  lr.routing);
    if (lr.routing.masterBus.id.isNull())
        lr.routing = ProjectRouting::createDefault();
    return lr;
}

// ── Atomic save ───────────────────────────────────────────────────
bool SessionSchema::platformFsync(const juce::File& file) noexcept {
#if JUCE_WINDOWS
    HANDLE h = CreateFileW(file.getFullPathName().toWideCharPointer(),
                           GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = FlushFileBuffers(h) != 0;
    CloseHandle(h); return ok;
#else
    int fd = ::open(file.getFullPathName().toRawUTF8(), O_RDWR);
    if (fd < 0) return false;
    bool ok = ::fsync(fd) == 0;
    ::close(fd); return ok;
#endif
}

juce::Result SessionSchema::atomicSave(const Y2KSession& session,
                                        const ProjectRouting& routing,
                                        const juce::File& dest) {
    auto xml = serialise(session, routing).createXml();
    if (!xml) return juce::Result::fail("XML serialise failed");

    juce::File tmp = dest.getSiblingFile(
        dest.getFileNameWithoutExtension() + ".tmp");
    if (!tmp.replaceWithText(xml->toString()))
        return juce::Result::fail("Write tmp failed: " + tmp.getFullPathName());

    platformFsync(tmp);  // Best-effort flush

    if (!tmp.moveFileTo(dest)) {
        tmp.deleteFile();
        return juce::Result::fail("Rename failed: " + dest.getFullPathName());
    }
    return juce::Result::ok();
}

juce::Result SessionSchema::autoSave(const Y2KSession& session,
                                      const ProjectRouting& routing,
                                      const juce::File& dest) {
    auto xml = serialise(session, routing).createXml();
    if (!xml) return juce::Result::fail("XML serialise failed");
    if (!dest.replaceWithText(xml->toString()))
        return juce::Result::fail("Auto-save write failed");
    return juce::Result::ok();
}

bool SessionSchema::autoSaveIsNewer(const juce::File& main,
                                     const juce::File& autosv) noexcept {
    if (!autosv.existsAsFile()) return false;
    if (!main.existsAsFile()) return true;
    return autosv.getLastModificationTime() > main.getLastModificationTime();
}

} // namespace y2k::engine
