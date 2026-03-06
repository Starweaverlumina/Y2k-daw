// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/autoplay.cpp
// ═══════════════════════════════════════════════════════════════════

#include "autoplay.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <random>

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  Scale interval tables (semitones from root)
// ─────────────────────────────────────────────────────────────────
std::vector<int> AutoPlayGenerator::scaleIntervals(Scale scale) {
    switch (scale) {
        case Scale::CMajor:
        case Scale::AMinor:
            return { 0, 2, 4, 5, 7, 9, 11 };
        case Scale::DPentatonic:
            return { 0, 2, 4, 7, 9 };
        case Scale::Blues:
            return { 0, 3, 5, 6, 7, 10 };
        case Scale::Chromatic:
        case Scale::Custom:
        default:
            return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    }
}

std::vector<int> AutoPlayGenerator::notesForScale(Scale scale, int rootNote,
                                                    int octaveRange)
{
    const auto intervals = scaleIntervals(scale);
    std::vector<int> notes;
    notes.reserve(intervals.size() * static_cast<size_t>(octaveRange));

    for (int oct = 0; oct < octaveRange; ++oct) {
        for (int semitone : intervals) {
            const int note = rootNote + oct * 12 + semitone;
            if (note >= 0 && note <= 127)
                notes.push_back(note);
        }
    }
    return notes;
}

// ─────────────────────────────────────────────────────────────────
//  generate  [thread-safe pure function]
//
//  Algorithm:
//  1. Build a flat list of allowed MIDI pitches.
//  2. For each grid step in (bars * gridDivision) positions:
//       - Roll density die → place note or skip
//       - Pick note from allowed list (weighted toward middle of range)
//       - Pick velocity in [velocityMin, velocityMax]
//       - Pick duration with variance
//       - Apply swing to beat position
//  3. Return as MidiClip.
// ─────────────────────────────────────────────────────────────────
std::unique_ptr<MidiClip> AutoPlayGenerator::generate(const AutoPlayParams& p) const {
    // Build note pool
    std::vector<int> pool;
    if (p.scale == Scale::Custom && !p.allowedNotes.empty()) {
        pool = p.allowedNotes;
    } else {
        pool = notesForScale(p.scale, p.rootNote, p.octaveRange);
    }
    if (pool.empty()) pool = { p.rootNote };

    // Seed RNG
    const uint32_t seed = (p.seed != 0) ? p.seed
        : static_cast<uint32_t>(std::time(nullptr));
    std::mt19937 rng(seed);

    std::uniform_real_distribution<float> distF(0.f, 1.f);
    std::uniform_int_distribution<int>    distNote(0, static_cast<int>(pool.size()) - 1);

    // Total grid steps
    const double beatsPerBar  = 4.0;
    const double totalBeats   = p.bars * beatsPerBar;
    const double stepBeats    = beatsPerBar / p.gridDivision;
    const int    totalSteps   = static_cast<int>(totalBeats / stepBeats);

    auto clip = std::make_unique<MidiClip>();
    clip->name          = p.clipName;
    clip->startBeat     = p.startBeat;
    clip->durationBeats = totalBeats;
    clip->gridDivision  = p.gridDivision;
    clip->color         = TrackColor::Lime;  // AutoPlay clips are lime-colored

    for (int step = 0; step < totalSteps; ++step) {
        // Density gate
        if (distF(rng) > p.density) continue;

        // Beat position with optional swing on odd 8th-note steps
        double beat = step * stepBeats;
        if (p.swingAmount > 0.f) {
            // Swing: push even sub-beats back by swingAmount * stepBeats/2
            const bool isSwingStep = (step % 2) == 1;
            if (isSwingStep)
                beat += p.swingAmount * stepBeats * 0.33;
        }

        // Note selection — slightly weighted toward middle of pool
        const int noteIdx = distNote(rng);
        const int pitch   = pool[static_cast<size_t>(noteIdx)];

        // Velocity
        const float vel = p.velocityMin +
            distF(rng) * (p.velocityMax - p.velocityMin);

        // Duration — base + variance
        const double durBase  = p.noteDuration;
        const float  varScale = 1.f + (distF(rng) * 2.f - 1.f) * p.durationVar;
        const double dur = std::max(stepBeats * 0.1,
                                    static_cast<double>(durBase * varScale));
        // Clamp so note doesn't overrun clip
        const double clampedDur = std::min(dur, totalBeats - beat - 0.01);
        if (clampedDur <= 0.0) continue;

        MidiClip::Note note;
        note.pitch         = pitch;
        note.velocity      = vel;
        note.startBeat     = beat;
        note.durationBeats = clampedDur;
        clip->notes.push_back(note);
    }

    return clip;
}

} // namespace y2k::engine
