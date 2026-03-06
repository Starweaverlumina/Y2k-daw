// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/mixer/track_model.cpp  — v9
// ═══════════════════════════════════════════════════════════════════
#include "track_model.h"
#include <unordered_set>

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  ProjectRouting::createDefault
//  4 instrument tracks → master bus
// ─────────────────────────────────────────────────────────────────
ProjectRouting ProjectRouting::createDefault() {
    ProjectRouting r;

    r.masterBus.id       = juce::Uuid();
    r.masterBus.name     = "Master";
    r.masterBus.isMaster = true;

    static const char* names[] = {"Track 1","Track 2","Track 3","Track 4"};
    static const char* insts[] = {"SineOsc","BondiSynth","SineOsc","SineOsc"};

    for (int i = 0; i < 4; ++i) {
        TrackModel t;
        t.id           = juce::Uuid();
        t.name         = names[i];
        t.inputType    = TrackInputType::Instrument;
        t.instrumentId = insts[i];
        t.volume       = 0.8f;
        t.pan          = 0.f;
        // No bus output specified → routes to master
        r.tracks.push_back(std::move(t));
    }

    return r;
}

// ─────────────────────────────────────────────────────────────────
//  ProjectRouting::validate
// ─────────────────────────────────────────────────────────────────
juce::Result ProjectRouting::validate() const {
    // Check master bus exists
    if (masterBus.id.isNull())
        return juce::Result::fail("ProjectRouting: masterBus has null ID");

    // Check no duplicate track IDs
    std::unordered_set<juce::String> trackIds;
    for (const auto& t : tracks) {
        const auto sid = t.id.toString();
        if (trackIds.count(sid))
            return juce::Result::fail("ProjectRouting: duplicate track ID " + sid);
        trackIds.insert(sid);
    }

    // Check no duplicate bus IDs
    std::unordered_set<juce::String> busIds;
    busIds.insert(masterBus.id.toString());
    for (const auto& b : buses) {
        const auto sid = b.id.toString();
        if (busIds.count(sid))
            return juce::Result::fail("ProjectRouting: duplicate bus ID " + sid);
        busIds.insert(sid);
    }

    // Check all send targets exist
    for (const auto& t : tracks) {
        for (const auto& send : t.sends) {
            if (!busIds.count(send.targetBusId.toString()))
                return juce::Result::fail(
                    "ProjectRouting: track '" + t.name +
                    "' send targets unknown bus " + send.targetBusId.toString());
        }
        // Check outputBus exists if specified
        if (t.outputBusId.has_value()) {
            if (!busIds.count(t.outputBusId->toString()))
                return juce::Result::fail(
                    "ProjectRouting: track '" + t.name +
                    "' routes to unknown bus " + t.outputBusId->toString());
        }
    }

    return juce::Result::ok();
}

const BusModel* ProjectRouting::findBus(const juce::Uuid& id) const noexcept {
    if (masterBus.id == id) return &masterBus;
    for (const auto& b : buses) if (b.id == id) return &b;
    return nullptr;
}

BusModel* ProjectRouting::findBus(const juce::Uuid& id) noexcept {
    if (masterBus.id == id) return &masterBus;
    for (auto& b : buses) if (b.id == id) return &b;
    return nullptr;
}

} // namespace y2k::engine
