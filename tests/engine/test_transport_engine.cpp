#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_engine/transport_engine.h"

using namespace y2k::engine;
using Catch::Approx;

TEST_CASE("TransportEngine: initial state stopped at zero", "[transport]") {
    TransportEngine t;
    REQUIRE(!t.isPlaying());
    REQUIRE(t.beatPosition() == Approx(0.0));
    REQUIRE(t.bpm()          == Approx(120.0));
}

TEST_CASE("TransportEngine: tick does not advance when stopped", "[transport]") {
    TransportEngine t;
    t.setSampleRate(44100.0);
    t.setBPM(120.0);
    t.tick(1024);
    REQUIRE(t.beatPosition() == Approx(0.0));
}

TEST_CASE("TransportEngine: tick advances when playing", "[transport]") {
    TransportEngine t;
    t.setSampleRate(44100.0);
    t.setBPM(120.0);
    t.play();
    // 120 BPM = 2 beats/sec; 44100 samples/sec → 1 beat = 22050 samples
    t.tick(22050);
    REQUIRE(t.beatPosition() == Approx(1.0).margin(0.001));
}

TEST_CASE("TransportEngine: tick returns beat at block start", "[transport]") {
    TransportEngine t;
    t.setSampleRate(44100.0);
    t.setBPM(120.0);
    t.play();
    const double start = t.tick(512);
    REQUIRE(start == Approx(0.0));
    const double start2 = t.tick(512);
    REQUIRE(start2 > 0.0);
}

TEST_CASE("TransportEngine: reset returns to zero", "[transport]") {
    TransportEngine t;
    t.setSampleRate(44100.0);
    t.setBPM(120.0);
    t.play();
    t.tick(44100);
    t.reset();
    REQUIRE(t.beatPosition() == Approx(0.0));
}

TEST_CASE("TransportEngine: BPM change is sample-accurate (next tick)", "[transport]") {
    TransportEngine t;
    t.setSampleRate(44100.0);
    t.setBPM(120.0);
    t.play();
    t.tick(22050); // +1 beat
    t.setBPM(60.0);
    t.tick(44100); // at 60 BPM, 44100 samples = 1 beat
    REQUIRE(t.beatPosition() == Approx(2.0).margin(0.001));
}

TEST_CASE("TransportEngine: looping wraps beat position", "[transport]") {
    TransportEngine t;
    t.setSampleRate(44100.0);
    t.setBPM(120.0);
    t.setLooping(true, 0.0, 2.0); // loop 2 beats
    t.play();
    // Advance 3 beats (66150 samples at 120 BPM)
    t.tick(66150);
    REQUIRE(t.beatPosition() < 2.0);
    REQUIRE(t.beatPosition() >= 0.0);
}

TEST_CASE("TransportEngine: barBeat breakdown (4/4)", "[transport]") {
    TransportEngine t;
    t.setSampleRate(44100.0);
    t.setBPM(120.0);
    t.play();
    t.tick(22050 * 5); // advance 5 beats → bar 2, beat 2
    auto bb = t.barBeat();
    REQUIRE(bb.bar  == 2);
    REQUIRE(bb.beat == 2);
}

TEST_CASE("TransportEngine: beatsToSeconds correct", "[transport]") {
    TransportEngine t;
    t.setBPM(120.0);
    REQUIRE(t.beatsToSeconds(2.0) == Approx(1.0)); // 2 beats at 120BPM = 1 sec
    t.setBPM(60.0);
    REQUIRE(t.beatsToSeconds(1.0) == Approx(1.0)); // 1 beat at 60BPM = 1 sec
}
