#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_engine/autoplay.h"

using namespace y2k::engine;
using Catch::Approx;

TEST_CASE("AutoPlayGenerator: generates clip with correct duration", "[autoplay]") {
    AutoPlayParams p;
    p.bars = 4;
    p.seed = 12345;
    p.density = 1.0f;  // fill all positions

    AutoPlayGenerator gen;
    auto clip = gen.generate(p);

    REQUIRE(clip != nullptr);
    REQUIRE(clip->durationBeats == Approx(16.0));  // 4 bars * 4 beats
}

TEST_CASE("AutoPlayGenerator: respects density=0 (empty clip)", "[autoplay]") {
    AutoPlayParams p;
    p.bars    = 4;
    p.seed    = 42;
    p.density = 0.0f;

    AutoPlayGenerator gen;
    auto clip = gen.generate(p);

    REQUIRE(clip != nullptr);
    REQUIRE(clip->notes.empty());
}

TEST_CASE("AutoPlayGenerator: all notes in allowed set", "[autoplay]") {
    AutoPlayParams p;
    p.scale     = Scale::Custom;
    p.allowedNotes = { 60, 62, 64, 65, 67, 69, 71, 72 };
    p.density   = 1.0f;
    p.bars      = 2;
    p.seed      = 99;

    AutoPlayGenerator gen;
    auto clip = gen.generate(p);

    for (const auto& note : clip->notes) {
        const bool found = std::find(p.allowedNotes.begin(),
                                      p.allowedNotes.end(),
                                      note.pitch) != p.allowedNotes.end();
        REQUIRE(found);
    }
}

TEST_CASE("AutoPlayGenerator: notes have valid MIDI range", "[autoplay]") {
    AutoPlayParams p;
    p.scale    = Scale::Blues;
    p.rootNote = 48;
    p.density  = 0.8f;
    p.bars     = 8;
    p.seed     = 7777;

    AutoPlayGenerator gen;
    auto clip = gen.generate(p);

    for (const auto& note : clip->notes) {
        REQUIRE(note.pitch >= 0);
        REQUIRE(note.pitch <= 127);
        REQUIRE(note.velocity >= 0.f);
        REQUIRE(note.velocity <= 1.f);
        REQUIRE(note.durationBeats > 0.0);
        REQUIRE(note.startBeat >= 0.0);
        REQUIRE(note.startBeat < clip->durationBeats);
    }
}

TEST_CASE("AutoPlayGenerator: same seed produces same clip", "[autoplay]") {
    AutoPlayParams p;
    p.density = 0.6f;
    p.bars    = 4;
    p.seed    = 55555;

    AutoPlayGenerator gen;
    auto c1 = gen.generate(p);
    auto c2 = gen.generate(p);

    REQUIRE(c1->notes.size() == c2->notes.size());
    for (size_t i = 0; i < c1->notes.size(); ++i) {
        REQUIRE(c1->notes[i].pitch == c2->notes[i].pitch);
        REQUIRE(c1->notes[i].startBeat == Approx(c2->notes[i].startBeat));
    }
}

TEST_CASE("AutoPlayGenerator: scale intervals correct", "[autoplay]") {
    auto major = AutoPlayGenerator::scaleIntervals(Scale::CMajor);
    REQUIRE(major.size() == 7);
    REQUIRE(major[0] == 0);
    REQUIRE(major[4] == 7);  // Perfect 5th

    auto penta = AutoPlayGenerator::scaleIntervals(Scale::DPentatonic);
    REQUIRE(penta.size() == 5);

    auto blues = AutoPlayGenerator::scaleIntervals(Scale::Blues);
    REQUIRE(blues.size() == 6);
}
