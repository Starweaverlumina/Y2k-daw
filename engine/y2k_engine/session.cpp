// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/session.cpp
//  Y2KSession — save/load .y2k bundle, track/clip management
// ═══════════════════════════════════════════════════════════════════

#include "session.h"

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  Color helpers
// ─────────────────────────────────────────────────────────────────
juce::Colour trackColorToJuce(TrackColor c) noexcept {
    switch (c) {
        case TrackColor::BondiBlue:      return juce::Colour(0xFF0071E3);
        case TrackColor::Tangerine:      return juce::Colour(0xFFFF9500);
        case TrackColor::Strawberry:     return juce::Colour(0xFFFF2D55);
        case TrackColor::Grape:          return juce::Colour(0xFFAF52DE);
        case TrackColor::Lime:           return juce::Colour(0xFFA6E22E);
        case TrackColor::FlowerPower:    return juce::Colour(0xFFFF69B4);
        case TrackColor::BlueDalmatian:  return juce::Colour(0xFF2E4C8A);
        case TrackColor::Graphite:       return juce::Colour(0xFF5E5E5E);
        default:                         return juce::Colour(0xFF0071E3);
    }
}

juce::String trackColorName(TrackColor c) noexcept {
    switch (c) {
        case TrackColor::BondiBlue:     return "BondiBlue";
        case TrackColor::Tangerine:     return "Tangerine";
        case TrackColor::Strawberry:    return "Strawberry";
        case TrackColor::Grape:         return "Grape";
        case TrackColor::Lime:          return "Lime";
        case TrackColor::FlowerPower:   return "FlowerPower";
        case TrackColor::BlueDalmatian: return "BlueDalmatian";
        case TrackColor::Graphite:      return "Graphite";
        default:                        return "Unknown";
    }
}

// ─────────────────────────────────────────────────────────────────
//  AutomationLane::evaluate
// ─────────────────────────────────────────────────────────────────
float AutomationLane::evaluate(double beat) const noexcept {
    if (points.empty()) return 0.f;
    if (beat <= points.front().beat) return points.front().value;
    if (beat >= points.back().beat)  return points.back().value;

    for (size_t i = 1; i < points.size(); ++i) {
        const auto& a = points[i - 1];
        const auto& b = points[i];
        if (beat >= a.beat && beat <= b.beat) {
            const double t = (beat - a.beat) / (b.beat - a.beat);
            switch (curve) {
                case AutomationCurve::Exponential:
                    return a.value + (b.value - a.value) * static_cast<float>(t * t);
                case AutomationCurve::Stepped:
                    return a.value;
                default:
                    return a.value + (b.value - a.value) * static_cast<float>(t);
            }
        }
    }
    return points.back().value;
}

// ─────────────────────────────────────────────────────────────────
//  Clip serialization
// ─────────────────────────────────────────────────────────────────
juce::ValueTree Clip::toValueTree() const {
    juce::ValueTree vt("Clip");
    vt.setProperty("id",            id.toString(),                       nullptr);
    vt.setProperty("name",          name,                                nullptr);
    vt.setProperty("startBeat",     startBeat,                           nullptr);
    vt.setProperty("durationBeats", durationBeats,                       nullptr);
    vt.setProperty("loopStart",     loopStart,                           nullptr);
    vt.setProperty("loopLength",    loopLength,                          nullptr);
    vt.setProperty("color",         static_cast<int>(color),             nullptr);
    vt.setProperty("fadeIn",        fades.inBeats,                       nullptr);
    vt.setProperty("fadeOut",       fades.outBeats,                      nullptr);
    return vt;
}

juce::ValueTree AudioClip::toValueTree() const {
    auto vt = Clip::toValueTree();
    vt.setProperty("type",          "Audio",                             nullptr);
    vt.setProperty("sourceFile",    sourceFile.getFullPathName(),        nullptr);
    vt.setProperty("sourceOffset",  sourceOffsetSec,                     nullptr);
    vt.setProperty("stretchSpeed",  stretchSpeed,                        nullptr);
    vt.setProperty("stretchPitch",  stretchPitch,                        nullptr);
    return vt;
}

juce::ValueTree MidiClip::toValueTree() const {
    auto vt = Clip::toValueTree();
    vt.setProperty("type",         "MIDI",       nullptr);
    vt.setProperty("gridDivision", gridDivision, nullptr);

    juce::ValueTree notesVt("Notes");
    for (const auto& n : notes) {
        juce::ValueTree nv("Note");
        nv.setProperty("pitch",    n.pitch,         nullptr);
        nv.setProperty("vel",      n.velocity,      nullptr);
        nv.setProperty("start",    n.startBeat,     nullptr);
        nv.setProperty("dur",      n.durationBeats, nullptr);
        notesVt.appendChild(nv, nullptr);
    }
    vt.appendChild(notesVt, nullptr);
    return vt;
}

std::unique_ptr<Clip> Clip::fromValueTree(const juce::ValueTree& vt) {
    const juce::String type = vt.getProperty("type", "Audio").toString();

    std::unique_ptr<Clip> clip;
    if (type == "MIDI") {
        auto mc = std::make_unique<MidiClip>();
        mc->gridDivision = static_cast<int>(vt.getProperty("gridDivision", 16));
        auto notesVt = vt.getChildWithName("Notes");
        for (int i = 0; i < notesVt.getNumChildren(); ++i) {
            auto nv = notesVt.getChild(i);
            MidiClip::Note n;
            n.pitch         = static_cast<int>(nv.getProperty("pitch", 60));
            n.velocity      = static_cast<float>(nv.getProperty("vel", 0.8f));
            n.startBeat     = static_cast<double>(nv.getProperty("start", 0.0));
            n.durationBeats = static_cast<double>(nv.getProperty("dur", 1.0));
            mc->notes.push_back(n);
        }
        clip = std::move(mc);
    } else {
        auto ac = std::make_unique<AudioClip>();
        ac->sourceFile      = juce::File(vt.getProperty("sourceFile", "").toString());
        ac->sourceOffsetSec = static_cast<double>(vt.getProperty("sourceOffset", 0.0));
        ac->stretchSpeed    = static_cast<float>(vt.getProperty("stretchSpeed", 1.f));
        ac->stretchPitch    = static_cast<float>(vt.getProperty("stretchPitch", 0.f));
        clip = std::move(ac);
    }

    clip->id            = juce::Uuid(vt.getProperty("id", juce::Uuid().toString()).toString());
    clip->name          = vt.getProperty("name", "Clip").toString();
    clip->startBeat     = static_cast<double>(vt.getProperty("startBeat",     0.0));
    clip->durationBeats = static_cast<double>(vt.getProperty("durationBeats", 4.0));
    clip->loopStart     = static_cast<double>(vt.getProperty("loopStart",     0.0));
    clip->loopLength    = static_cast<double>(vt.getProperty("loopLength",    0.0));
    clip->color         = static_cast<TrackColor>(static_cast<int>(vt.getProperty("color", 0)));
    clip->fades.inBeats  = static_cast<double>(vt.getProperty("fadeIn",  0.0));
    clip->fades.outBeats = static_cast<double>(vt.getProperty("fadeOut", 0.0));
    return clip;
}

// ─────────────────────────────────────────────────────────────────
//  Track serialization
// ─────────────────────────────────────────────────────────────────
juce::ValueTree Track::toValueTree() const {
    juce::ValueTree vt("Track");
    vt.setProperty("id",          id.toString(),                 nullptr);
    vt.setProperty("name",        name,                          nullptr);
    vt.setProperty("type",        static_cast<int>(type),        nullptr);
    vt.setProperty("color",       static_cast<int>(color),       nullptr);
    vt.setProperty("muted",       isMuted,                       nullptr);
    vt.setProperty("soloed",      isSoloed,                      nullptr);
    vt.setProperty("armed",       isArmed,                       nullptr);
    vt.setProperty("volume",      volume,                        nullptr);
    vt.setProperty("pan",         pan,                           nullptr);
    vt.setProperty("pixelHeight", pixelHeight,                   nullptr);

    juce::ValueTree clipsVt("Clips");
    for (const auto& c : clips)
        clipsVt.appendChild(c->toValueTree(), nullptr);
    vt.appendChild(clipsVt, nullptr);
    return vt;
}

std::unique_ptr<Track> Track::fromValueTree(const juce::ValueTree& vt) {
    auto t = std::make_unique<Track>();
    t->id          = juce::Uuid(vt.getProperty("id", juce::Uuid().toString()).toString());
    t->name        = vt.getProperty("name",        "Track").toString();
    t->type        = static_cast<Type>(static_cast<int>(vt.getProperty("type", 0)));
    t->color       = static_cast<TrackColor>(static_cast<int>(vt.getProperty("color", 0)));
    t->isMuted     = static_cast<bool>(vt.getProperty("muted",   false));
    t->isSoloed    = static_cast<bool>(vt.getProperty("soloed",  false));
    t->isArmed     = static_cast<bool>(vt.getProperty("armed",   false));
    t->volume      = static_cast<float>(vt.getProperty("volume",      80.f));
    t->pan         = static_cast<float>(vt.getProperty("pan",          0.f));
    t->pixelHeight = static_cast<int>(vt.getProperty("pixelHeight",   80));

    auto clipsVt = vt.getChildWithName("Clips");
    for (int i = 0; i < clipsVt.getNumChildren(); ++i) {
        auto clip = Clip::fromValueTree(clipsVt.getChild(i));
        if (clip) t->clips.push_back(std::move(clip));
    }
    return t;
}

// ─────────────────────────────────────────────────────────────────
//  Y2KSession::track management
// ─────────────────────────────────────────────────────────────────
Track* Y2KSession::addTrack(Track::Type type, const juce::String& name) {
    auto t = std::make_unique<Track>();
    t->type  = type;
    t->name  = name.isEmpty() ? "Track " + juce::String(tracks.size() + 1) : name;
    t->color = static_cast<TrackColor>(tracks.size() % static_cast<int>(TrackColor::COUNT));
    Track* raw = t.get();
    tracks.push_back(std::move(t));
    modifiedDate = juce::Time::getCurrentTime();
    return raw;
}

bool Y2KSession::removeTrack(const juce::Uuid& trackId) {
    auto it = std::find_if(tracks.begin(), tracks.end(),
        [&](const auto& t) { return t->id == trackId; });
    if (it == tracks.end()) return false;
    tracks.erase(it);
    modifiedDate = juce::Time::getCurrentTime();
    return true;
}

Track* Y2KSession::findTrack(const juce::Uuid& trackId) {
    for (auto& t : tracks)
        if (t->id == trackId) return t.get();
    return nullptr;
}

int Y2KSession::getTrackIndex(const juce::Uuid& trackId) const {
    for (int i = 0; i < (int)tracks.size(); ++i)
        if (tracks[i]->id == trackId) return i;
    return -1;
}

void Y2KSession::reorderTrack(int fromIdx, int toIdx) {
    if (fromIdx == toIdx) return;
    if (fromIdx < 0 || fromIdx >= (int)tracks.size()) return;
    if (toIdx   < 0 || toIdx   >= (int)tracks.size()) return;
    auto t = std::move(tracks[fromIdx]);
    tracks.erase(tracks.begin() + fromIdx);
    tracks.insert(tracks.begin() + toIdx, std::move(t));
    modifiedDate = juce::Time::getCurrentTime();
}

bool Y2KSession::isValid() const {
    return !projectName.isEmpty() && timeline.tempo > 0.0 && timeline.sampleRate > 0;
}

// ─────────────────────────────────────────────────────────────────
//  Y2KSession::createEmpty / createFromTemplate
// ─────────────────────────────────────────────────────────────────
std::unique_ptr<Y2KSession> Y2KSession::createEmpty() {
    auto s = std::make_unique<Y2KSession>();
    s->id           = juce::Uuid();
    s->projectName  = "Untitled";
    s->createdDate  = juce::Time::getCurrentTime();
    s->modifiedDate = s->createdDate;
    return s;
}

std::unique_ptr<Y2KSession> Y2KSession::createFromTemplate(const juce::String& templateName) {
    auto s = createEmpty();
    s->projectName = templateName;

    // Default 8-track template matching the Y2K palette
    struct TrackDef { const char* name; Track::Type type; TrackColor color; };
    static const TrackDef defs[] = {
        { "Bondi Bass",    Track::Type::Instrument, TrackColor::BondiBlue     },
        { "Tang Lead",     Track::Type::Instrument, TrackColor::Tangerine     },
        { "Grape Pads",    Track::Type::Instrument, TrackColor::Grape         },
        { "Lime Synth",    Track::Type::Instrument, TrackColor::Lime          },
        { "Flower Drums",  Track::Type::Instrument, TrackColor::FlowerPower   },
        { "Strawberry FX", Track::Type::Aux,        TrackColor::Strawberry    },
        { "Graphite Vox",  Track::Type::Audio,      TrackColor::Graphite      },
        { "Bondi Aux",     Track::Type::Aux,        TrackColor::BlueDalmatian },
    };

    for (const auto& d : defs) {
        auto* t  = s->addTrack(d.type, d.name);
        t->color = d.color;
    }
    return s;
}

// ─────────────────────────────────────────────────────────────────
//  JSON / ValueTree serialization
// ─────────────────────────────────────────────────────────────────
void Y2KSession::writeProjectJson(const juce::File& dest) const {
    juce::ValueTree root("Y2KSession");
    root.setProperty("formatVersion", FORMAT_VERSION,                 nullptr);
    root.setProperty("id",            id.toString(),                  nullptr);
    root.setProperty("projectName",   projectName,                    nullptr);
    root.setProperty("author",        author,                         nullptr);
    root.setProperty("dawVersion",    dawVersion,                     nullptr);
    root.setProperty("tempo",         timeline.tempo,                 nullptr);
    root.setProperty("timeSigNum",    timeline.timeSigNum,            nullptr);
    root.setProperty("timeSigDen",    timeline.timeSigDen,            nullptr);
    root.setProperty("sampleRate",    timeline.sampleRate,            nullptr);
    root.setProperty("bitDepth",      timeline.bitDepth,              nullptr);
    root.setProperty("totalBeats",    timeline.totalBeats,            nullptr);
    root.setProperty("loopStart",     loopStartBeat,                  nullptr);
    root.setProperty("loopEnd",       loopEndBeat,                    nullptr);
    root.setProperty("loopEnabled",   loopEnabled,                    nullptr);

    juce::ValueTree tracksVt("Tracks");
    for (const auto& t : tracks)
        tracksVt.appendChild(t->toValueTree(), nullptr);
    root.appendChild(tracksVt, nullptr);

    juce::ValueTree markersVt("Markers");
    for (const auto& m : markers) {
        juce::ValueTree mv("Marker");
        mv.setProperty("beat",  m.beat,                             nullptr);
        mv.setProperty("label", m.label,                            nullptr);
        mv.setProperty("color", static_cast<int>(m.color),          nullptr);
        markersVt.appendChild(mv, nullptr);
    }
    root.appendChild(markersVt, nullptr);

    // Serialise as JUCE XML (portable, no extra deps)
    auto xml = root.createXml();
    if (xml) xml->writeTo(dest);
}

bool Y2KSession::readProjectJson(const juce::File& src) {
    auto xml = juce::XmlDocument::parse(src);
    if (!xml) return false;
    auto root = juce::ValueTree::fromXml(*xml);
    if (!root.isValid()) return false;

    id           = juce::Uuid(root.getProperty("id", juce::Uuid().toString()).toString());
    projectName  = root.getProperty("projectName", "Untitled").toString();
    author       = root.getProperty("author",       "").toString();
    dawVersion   = root.getProperty("dawVersion",   "1.0.0").toString();

    timeline.tempo      = static_cast<double>(root.getProperty("tempo",      120.0));
    timeline.timeSigNum = static_cast<int>   (root.getProperty("timeSigNum", 4));
    timeline.timeSigDen = static_cast<int>   (root.getProperty("timeSigDen", 4));
    timeline.sampleRate = static_cast<int>   (root.getProperty("sampleRate", 44100));
    timeline.bitDepth   = static_cast<int>   (root.getProperty("bitDepth",   24));
    timeline.totalBeats = static_cast<double>(root.getProperty("totalBeats", 128.0));
    loopStartBeat       = static_cast<double>(root.getProperty("loopStart",  0.0));
    loopEndBeat         = static_cast<double>(root.getProperty("loopEnd",    8.0));
    loopEnabled         = static_cast<bool>  (root.getProperty("loopEnabled", false));

    tracks.clear();
    auto tracksVt = root.getChildWithName("Tracks");
    for (int i = 0; i < tracksVt.getNumChildren(); ++i) {
        auto t = Track::fromValueTree(tracksVt.getChild(i));
        if (t) tracks.push_back(std::move(t));
    }

    markers.clear();
    auto markersVt = root.getChildWithName("Markers");
    for (int i = 0; i < markersVt.getNumChildren(); ++i) {
        auto mv = markersVt.getChild(i);
        ArrangementMarker m;
        m.beat  = static_cast<double>(mv.getProperty("beat",  0.0));
        m.label = mv.getProperty("label", "").toString();
        m.color = static_cast<TrackColor>(static_cast<int>(mv.getProperty("color", 0)));
        markers.push_back(m);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  Y2KSession::save / load  — .y2k bundle (directory package)
//
//  Bundle layout:
//    project.y2k/
//      project.xml       ← serialized session (ValueTree XML)
//      audio/            ← embedded audio files (copies)
//      previews/         ← waveform cache images
// ─────────────────────────────────────────────────────────────────
juce::Result Y2KSession::save(const juce::File& bundleDir) {
    if (!bundleDir.createDirectory())
        return juce::Result::fail("Could not create bundle directory: " + bundleDir.getFullPathName());

    // Write session XML
    const juce::File projectFile = bundleDir.getChildFile("project.xml");
    writeProjectJson(projectFile);

    // Copy referenced audio files into bundle/audio/
    const juce::File audioDir = bundleDir.getChildFile("audio");
    audioDir.createDirectory();

    for (const auto& track : tracks) {
        for (const auto& clip : track->clips) {
            if (auto* ac = dynamic_cast<AudioClip*>(clip.get())) {
                if (ac->sourceFile.existsAsFile()) {
                    const juce::File dest = audioDir.getChildFile(ac->sourceFile.getFileName());
                    if (!dest.existsAsFile())
                        ac->sourceFile.copyFileTo(dest);
                }
            }
        }
    }

    modifiedDate = juce::Time::getCurrentTime();
    return juce::Result::ok();
}

juce::Result Y2KSession::load(const juce::File& bundleDir) {
    if (!bundleDir.isDirectory())
        return juce::Result::fail("Not a directory: " + bundleDir.getFullPathName());

    const juce::File projectFile = bundleDir.getChildFile("project.xml");
    if (!projectFile.existsAsFile())
        return juce::Result::fail("project.xml not found in bundle");

    if (!readProjectJson(projectFile))
        return juce::Result::fail("Failed to parse project.xml");

    // Remap audio file paths relative to bundle
    const juce::File audioDir = bundleDir.getChildFile("audio");
    for (auto& track : tracks) {
        for (auto& clip : track->clips) {
            if (auto* ac = dynamic_cast<AudioClip*>(clip.get())) {
                const juce::File bundled = audioDir.getChildFile(ac->sourceFile.getFileName());
                if (bundled.existsAsFile())
                    ac->sourceFile = bundled;
            }
        }
    }
    return juce::Result::ok();
}

} // namespace y2k::engine
