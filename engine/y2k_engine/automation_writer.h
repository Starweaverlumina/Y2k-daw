#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/automation_writer.h
//
//  AutomationWriter — generates automation lanes + plays them back.
//
//  Phase 1 generates 2 lanes:
//    • Lane 0 = SineOsc frequency (440 → 880 Hz sweep, 8 bars)
//    • Lane 1 = TrackGain volume (fade-in / pulse pattern)
//
//  These are stored in Y2KSession::tracks[0]::automationLanes and
//  evaluated each audio block via AutomationPlayer.
//
//  AutomationPlayer::tick() is called from the audio callback;
//  it evaluates each lane at the current ppqPosition and pushes
//  parameter updates via std::atomic setters (no alloc, no locks).
// ═══════════════════════════════════════════════════════════════════

#include "session.h"
#include <functional>
#include <vector>

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  AutomationWriterParams
// ─────────────────────────────────────────────────────────────────
struct AutomationWriterParams {
    int     bars         = 8;
    int     pointsPerBar = 4;    // Automation resolution
    float   freqMin      = 220.f;  // Hz — filter/freq sweep low
    float   freqMax      = 880.f;  // Hz — filter/freq sweep high
    float   volMin       = 0.5f;
    float   volMax       = 1.0f;
    bool    looping      = true;
};

// ─────────────────────────────────────────────────────────────────
//  AutomationWriter — writes into Y2KSession
// ─────────────────────────────────────────────────────────────────
class AutomationWriter {
public:
    // Generate 2 automation lanes into track->automationLanes.
    // Clears existing lanes before writing.
    // Lane 0: "SineFreq"   — maps to SineOscProcessor param 0 (freq)
    // Lane 1: "TrackGain"  — maps to TrackGainProcessor param 0 (volume)
    static void generate(Track& track,
                         uint32_t sineOscNodeId,
                         uint32_t trackGainNodeId,
                         const AutomationWriterParams& p = {});
};

// ─────────────────────────────────────────────────────────────────
//  AutomationPlayer — evaluates lanes each audio block  [RT-safe]
//
//  Holds a lightweight snapshot of automation lanes (copied from
//  session on the UI thread). The audio thread only reads.
//
//  Dispatch callback: called with (nodeId, paramId, value) each block.
//  AudioEngine wires this to pushParameterChange().
// ─────────────────────────────────────────════════════════════════
using AutomationDispatch = std::function<void(uint32_t nodeId, int paramId, float value)>;

class AutomationPlayer {
public:
    AutomationPlayer() = default;

    // Non-RT: load lanes from session track (copies data, safe to call any time)
    void loadFromTrack(const Track& track);

    // Non-RT: clear all lanes
    void clear() noexcept { lanes_.clear(); totalBeats_ = 0.0; }

    // RT: evaluate all lanes at ppqBeat and dispatch parameter updates.
    // looping: if true, wraps ppqBeat within totalBeats_.
    void tick(double ppqBeat, bool isPlaying, const AutomationDispatch& dispatch) noexcept;

    bool hasLanes() const noexcept { return !lanes_.empty(); }
    int  laneCount() const noexcept { return static_cast<int>(lanes_.size()); }

private:
    // Lightweight copy of an automation lane for RT use
    struct RTLane {
        uint32_t nodeId;
        int      paramId;
        float    loopBeats;  // total duration of lane for looping
        std::vector<AutomationPoint> points;

        float evaluate(double beat) const noexcept;
    };

    std::vector<RTLane> lanes_;
    double              totalBeats_ = 32.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationPlayer)
};

} // namespace y2k::engine
