#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/autoplay.h
//
//  AutoPlayGenerator — converts a set of allowed notes + rhythm
//  density into a MidiClip that is inserted into the session.
//
//  This is the "plays the notes for you" feature.
//
//  Usage:
//    AutoPlayParams p;
//    p.allowedNotes = { 60, 62, 64, 65, 67, 69, 71, 72 }; // C major
//    p.density      = 0.6f;   // 60% of grid positions filled
//    p.bars         = 8;
//    p.seed         = 42;
//
//    AutoPlayGenerator gen;
//    auto clip = gen.generate(p);
//    // Insert clip into session track:
//    session.tracks[0]->clips.push_back(std::move(clip));
// ═══════════════════════════════════════════════════════════════════

#include "session.h"
#include <cstdint>
#include <vector>

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  Built-in scale definitions (root = MIDI note, e.g. 60 = C4)
// ─────────────────────────────────────────────────────────────────
enum class Scale {
    CMajor    = 0,
    AMinor    = 1,
    DPentatonic = 2,
    Blues     = 3,
    Chromatic = 4,
    Custom    = 5,   // use allowedNotes directly
};

// ─────────────────────────────────────────────────────────────────
//  AutoPlayParams
// ─────────────────────────────────────────────────────────────────
struct AutoPlayParams {
    // Note selection
    Scale             scale       = Scale::CMajor;
    int               rootNote    = 60;           // MIDI root (when using a scale)
    std::vector<int>  allowedNotes;               // Used when scale == Custom
    int               octaveRange = 2;            // How many octaves to span

    // Rhythm
    float   density       = 0.5f;   // 0 = sparse, 1 = every 16th note
    int     gridDivision  = 16;     // 8ths, 16ths, 32nds
    float   swingAmount   = 0.f;    // 0 = straight, 1 = max swing
    float   velocityMin   = 0.55f;
    float   velocityMax   = 0.90f;

    // Phrase
    int     bars          = 8;
    float   noteDuration  = 0.5f;   // Default note length in beats (0.25 = 16th)
    float   durationVar   = 0.3f;   // Randomness on note length (0-1)

    // Reproducibility
    uint32_t seed         = 0;      // 0 = use time-based seed

    // Clip metadata
    double   startBeat    = 0.0;
    juce::String clipName = "AutoPlay";
};

// ─────────────────────────────────────────────────────────────────
//  AutoPlayGenerator
// ─────────────────────────────────────────────────────────────────
class AutoPlayGenerator {
public:
    AutoPlayGenerator() = default;

    // Generate a MidiClip from params. Thread-safe (pure function, no state).
    std::unique_ptr<MidiClip> generate(const AutoPlayParams& params) const;

    // Helpers: build note lists from scale definitions
    static std::vector<int> notesForScale(Scale scale, int rootNote, int octaveRange);
    static std::vector<int> scaleIntervals(Scale scale);
};

} // namespace y2k::engine
