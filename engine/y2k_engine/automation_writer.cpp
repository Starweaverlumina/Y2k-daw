// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/automation_writer.cpp
// ═══════════════════════════════════════════════════════════════════

#include "automation_writer.h"
#include <algorithm>
#include <cmath>

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  AutomationWriter::generate
//
//  Lane 0 — SineFreq: slow logarithmic sweep freqMin→freqMax→freqMin
//            one complete cycle per 8 bars, normalised to [0,1]
//  Lane 1 — TrackGain: sinusoidal pulse pattern (breath-like)
//            peaks every 2 bars
// ─────────────────────────────────────────────────────────────────
void AutomationWriter::generate(Track& track,
                                 uint32_t sineOscNodeId,
                                 uint32_t trackGainNodeId,
                                 const AutomationWriterParams& p)
{
    track.automationLanes.clear();

    const double beatsPerBar  = 4.0;
    const double totalBeats   = p.bars * beatsPerBar;
    const int    totalPoints  = p.bars * p.pointsPerBar;
    const double stepBeats    = totalBeats / totalPoints;

    constexpr double kTwoPi = 6.283185307179586;

    // ── Lane 0: Sine oscillator frequency sweep ─────────────────
    {
        AutomationLane lane;
        lane.targetNodeId  = sineOscNodeId;
        lane.targetParamId = 0;    // SineOscProcessor param 0 = freq
        lane.paramName     = "SineFreq";
        lane.curve         = AutomationCurve::Linear;
        lane.isActive      = true;

        // Log sweep: freqMin → freqMax → freqMin over 8 bars
        // Normalise to [0,1] for the AutomationPoint; player scales back
        const float logMin = std::log2(p.freqMin);
        const float logMax = std::log2(p.freqMax);

        for (int i = 0; i <= totalPoints; ++i) {
            const double beat = i * stepBeats;
            const double phase = beat / totalBeats;  // 0..1

            // Full sine cycle: 0→1→0 over the phrase
            const float t = static_cast<float>(
                0.5 * (1.0 - std::cos(kTwoPi * phase)));
            // Interpolate in log space → exponential freq sweep
            const float logFreq = logMin + t * (logMax - logMin);
            const float normFreq = (logFreq - logMin) / (logMax - logMin);

            lane.points.push_back({ beat, normFreq });
        }
        track.automationLanes.push_back(std::move(lane));
    }

    // ── Lane 1: Per-track volume (gain) breathing pattern ────────
    {
        AutomationLane lane;
        lane.targetNodeId  = trackGainNodeId;
        lane.targetParamId = 0;    // TrackGainProcessor param 0 = volume
        lane.paramName     = "TrackVol";
        lane.curve         = AutomationCurve::Linear;
        lane.isActive      = true;

        for (int i = 0; i <= totalPoints; ++i) {
            const double beat  = i * stepBeats;
            const double phase = beat / (2.0 * beatsPerBar);  // cycle per 2 bars

            // Sinusoidal pulse between volMin and volMax
            const float t = static_cast<float>(
                0.5 * (1.0 - std::cos(kTwoPi * phase)));
            const float vol = p.volMin + t * (p.volMax - p.volMin);

            lane.points.push_back({ beat, vol });
        }
        track.automationLanes.push_back(std::move(lane));
    }
}

// ─────────────────────────────────────────────────────────────────
//  AutomationPlayer::RTLane::evaluate
// ─────────────────────────────────────────────────────────────────
float AutomationPlayer::RTLane::evaluate(double beat) const noexcept {
    if (points.empty()) return 0.f;

    // Loop within lane duration
    double b = beat;
    if (loopBeats > 0.f)
        b = std::fmod(b, static_cast<double>(loopBeats));

    if (b <= points.front().beat) return points.front().value;
    if (b >= points.back().beat)  return points.back().value;

    // Linear interpolation between adjacent points
    for (size_t i = 1; i < points.size(); ++i) {
        const auto& a = points[i - 1];
        const auto& pt = points[i];
        if (b >= a.beat && b <= pt.beat) {
            const float t = static_cast<float>(
                (b - a.beat) / (pt.beat - a.beat));
            return a.value + t * (pt.value - a.value);
        }
    }
    return points.back().value;
}

// ─────────────────────────────────────────────────────────────────
//  AutomationPlayer::loadFromTrack  [non-RT]
// ─────────────────────────────────────────────────────────────────
void AutomationPlayer::loadFromTrack(const Track& track) {
    lanes_.clear();
    totalBeats_ = 0.0;

    for (const auto& lane : track.automationLanes) {
        if (!lane.isActive || lane.points.empty()) continue;

        RTLane rt;
        rt.nodeId    = lane.targetNodeId;
        rt.paramId   = lane.targetParamId;
        rt.points    = lane.points;  // copy

        // Duration = last point beat
        rt.loopBeats = static_cast<float>(rt.points.back().beat);
        totalBeats_  = std::max(totalBeats_,
                                static_cast<double>(rt.loopBeats));
        lanes_.push_back(std::move(rt));
    }
}

// ─────────────────────────────────────────────────────────────────
//  AutomationPlayer::tick  [RT]
//
//  RT contract:
//    ✓ No allocation (RTLane::evaluate is pure arithmetic)
//    ✓ No locks
//    ✓ dispatch() must itself be RT-safe (atomic setters / paramQueue)
// ─────────────────────────────────────────────────────────────────
void AutomationPlayer::tick(double ppqBeat, bool isPlaying,
                             const AutomationDispatch& dispatch) noexcept
{
    if (!isPlaying || lanes_.empty() || !dispatch) return;

    for (const auto& lane : lanes_) {
        const float value = lane.evaluate(ppqBeat);
        dispatch(lane.nodeId, lane.paramId, value);
    }
}

} // namespace y2k::engine
