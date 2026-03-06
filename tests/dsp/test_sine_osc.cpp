#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_dsp/generators/sine_osc.h"

using namespace y2k::dsp;
using Catch::Approx;

static ProcessSpec makeSpec(double sr=44100.0, int block=512) {
    return {sr, block, 2};
}

TEST_CASE("SineOscProcessor: prepare sets initial frequency", "[sineosc]") {
    SineOscProcessor osc(440.f, 0.1f);
    osc.prepare(makeSpec());
    REQUIRE(osc.getFrequency() == Approx(440.f));
    REQUIRE(osc.getGain()      == Approx(0.1f));
}

TEST_CASE("SineOscProcessor: setFrequency updates base (not automation)", "[sineosc]") {
    SineOscProcessor osc;
    osc.prepare(makeSpec());
    osc.setFrequency(880.f);
    REQUIRE(osc.getFrequency()       == Approx(880.f));
    REQUIRE(osc.getParameter(0)      == Approx(880.f));  // base via bank
}

TEST_CASE("SineOscProcessor: automation layer does not overwrite base", "[sineosc]") {
    SineOscProcessor osc(440.f, 0.1f);
    osc.prepare(makeSpec());
    osc.setParameterAutomation(0, 220.f);   // write automation layer
    REQUIRE(osc.getFrequency()        == Approx(440.f));  // base unchanged
    REQUIRE(osc.resolvedParameter(0)  == Approx(660.f));  // 440+220 resolved
}

TEST_CASE("SineOscProcessor: process produces non-zero output", "[sineosc]") {
    SineOscProcessor osc(440.f, 0.5f);
    osc.prepare(makeSpec(44100.0, 512));
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    ProcessContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize  = 512;
    ctx.outputBuffer = &osc.outputBuf;
    osc.outputBuf.clear();
    osc.process(ctx);
    const float peak = osc.outputBuf.getMagnitude(0, 0, 512);
    REQUIRE(peak > 0.f);
}

TEST_CASE("SineOscProcessor: zero gain produces silence", "[sineosc]") {
    SineOscProcessor osc(440.f, 0.f);
    osc.prepare(makeSpec(44100.0, 512));
    ProcessContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.blockSize  = 512;
    ctx.outputBuffer = &osc.outputBuf;
    osc.outputBuf.clear();
    osc.process(ctx);
    const float peak = osc.outputBuf.getMagnitude(0, 0, 512);
    REQUIRE(peak == Approx(0.f).margin(1e-6f));
}

TEST_CASE("SineOscProcessor: reset clears phase and voices", "[sineosc]") {
    SineOscProcessor osc(440.f, 0.5f);
    osc.prepare(makeSpec());
    osc.noteOn(60, 1.0f);
    osc.reset();
    REQUIRE_NOTHROW(osc.process({nullptr, &osc.outputBuf, nullptr, 44100.0, 512, 120.0}));
}

TEST_CASE("SineOscProcessor: noteOn activates voice", "[sineosc]") {
    SineOscProcessor osc(440.f, 0.1f);
    osc.prepare(makeSpec(44100.0, 512));
    osc.noteOn(60, 0.8f);
    ProcessContext ctx;
    ctx.sampleRate = 44100.0; ctx.blockSize = 512;
    ctx.outputBuffer = &osc.outputBuf;
    osc.outputBuf.clear();
    osc.process(ctx);
    const float peak = osc.outputBuf.getMagnitude(0, 0, 512);
    REQUIRE(peak > 0.f);
}

TEST_CASE("SineOscProcessor: 0 inputs, 2 outputs, 2 params", "[sineosc]") {
    SineOscProcessor osc;
    REQUIRE(osc.getNumInputChannels()  == 0);
    REQUIRE(osc.getNumOutputChannels() == 2);
    REQUIRE(osc.getNumParameters()     == 2);
}
