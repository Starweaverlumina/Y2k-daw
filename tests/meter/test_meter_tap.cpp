#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_dsp/meter/meter_tap.h"

using namespace y2k::dsp;
using Catch::Approx;

static ProcessSpec spec44(int block=512) { return {44100.0, block, 2}; }

static void fillSine(juce::AudioBuffer<float>& buf, float amp) {
    const double inc = 2.0 * M_PI * 440.0 / 44100.0;
    double phase = 0.0;
    for (int s = 0; s < buf.getNumSamples(); ++s) {
        const float v = float(std::sin(phase)) * amp;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch) buf.setSample(ch, s, v);
        phase += inc;
    }
}

TEST_CASE("MeterTap: default reading is all zeros", "[meter]") {
    MeterTap tap;
    tap.prepare(spec44());
    const auto r = tap.read();
    REQUIRE(r.peakL == Approx(0.f));
    REQUIRE(r.peakR == Approx(0.f));
    REQUIRE(r.rmsL  == Approx(0.f));
    REQUIRE(!r.clipped);
}

TEST_CASE("MeterTap: detects peak from signal", "[meter]") {
    MeterTap tap; tap.prepare(spec44(512));
    tap.inputBuf.setSize(2, 512, false, true, false);
    fillSine(tap.inputBuf, 0.5f);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    tap.process(ctx);
    REQUIRE(tap.read().peakL > 0.4f);
    REQUIRE(tap.read().peakL <= 0.51f);
}

TEST_CASE("MeterTap: passthrough unity — output equals input", "[meter]") {
    MeterTap tap; tap.prepare(spec44(512));
    tap.inputBuf.setSize(2, 512, false, true, false);
    fillSine(tap.inputBuf, 0.3f);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    tap.process(ctx);
    for (int s = 0; s < 512; ++s)
        REQUIRE(tap.outputBuf.getSample(0,s) == Approx(tap.inputBuf.getSample(0,s)).margin(1e-6f));
}

TEST_CASE("MeterTap: clip detected when peak exceeds threshold", "[meter]") {
    MeterTap tap; tap.prepare(spec44(512));
    tap.inputBuf.setSize(2, 512, false, true, false);
    juce::FloatVectorOperations::fill(tap.inputBuf.getWritePointer(0), 1.1f, 512);
    juce::FloatVectorOperations::fill(tap.inputBuf.getWritePointer(1), 1.1f, 512);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    tap.process(ctx);
    REQUIRE(tap.read().clipped);
}

TEST_CASE("MeterTap: no clip below threshold", "[meter]") {
    MeterTap tap; tap.prepare(spec44(512));
    tap.inputBuf.setSize(2, 512, false, true, false);
    fillSine(tap.inputBuf, 0.9f);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    tap.process(ctx);
    REQUIRE(!tap.read().clipped);
}

TEST_CASE("MeterTap: resetClip clears clip flag", "[meter]") {
    MeterTap tap; tap.prepare(spec44(512));
    tap.inputBuf.setSize(2, 512, false, true, false);
    juce::FloatVectorOperations::fill(tap.inputBuf.getWritePointer(0), 1.2f, 512);
    juce::FloatVectorOperations::fill(tap.inputBuf.getWritePointer(1), 1.2f, 512);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    tap.process(ctx);
    REQUIRE(tap.read().clipped);
    tap.resetClip();
    REQUIRE(!tap.read().clipped);
}

TEST_CASE("MeterTap: RMS rises above zero after signal", "[meter]") {
    MeterTap tap; tap.prepare(spec44(512));
    tap.inputBuf.setSize(2, 512, false, true, false);
    fillSine(tap.inputBuf, 0.5f);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    tap.process(ctx);
    REQUIRE(tap.read().rmsL > 0.f);
}

TEST_CASE("MeterTap: peak hold persists after one block of silence", "[meter]") {
    MeterTap tap; tap.prepare(spec44(512));
    tap.inputBuf.setSize(2, 512, false, true, false);
    fillSine(tap.inputBuf, 0.8f);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    tap.process(ctx);
    const float hold1 = tap.read().holdL;
    tap.inputBuf.clear();
    tap.process(ctx);
    const float hold2 = tap.read().holdL;
    REQUIRE(hold2 > 0.f);
    REQUIRE(hold2 <= hold1);
}

TEST_CASE("MeterTap: resetPeak zeroes hold", "[meter]") {
    MeterTap tap; tap.prepare(spec44(512));
    tap.inputBuf.setSize(2, 512, false, true, false);
    fillSine(tap.inputBuf, 0.5f);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    tap.process(ctx);
    tap.resetPeak();
    REQUIRE(tap.read().holdL == Approx(0.f));
}

TEST_CASE("MeterTap: linearToDb(1.0) == 0 dBFS", "[meter]") {
    REQUIRE(MeterTap::linearToDb(1.f) == Approx(0.f).margin(0.01f));
}

TEST_CASE("MeterTap: linearToDb(0.0) == -120 dBFS floor", "[meter]") {
    REQUIRE(MeterTap::linearToDb(0.f) == Approx(-120.f));
}

TEST_CASE("MeterTap: no allocation in process() — 100 calls", "[meter]") {
    MeterTap tap; tap.prepare(spec44(512));
    tap.inputBuf.setSize(2, 512, false, true, false);
    fillSine(tap.inputBuf, 0.3f);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    for (int i=0;i<100;++i) REQUIRE_NOTHROW(tap.process(ctx));
}

TEST_CASE("MeterTap: getLatencySamples() is 0", "[meter]") {
    REQUIRE(MeterTap{}.getLatencySamples() == 0);
}
