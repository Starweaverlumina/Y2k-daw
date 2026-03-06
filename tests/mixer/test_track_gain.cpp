#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_dsp/mixer/track_gain.h"

using namespace y2k::dsp;
using Catch::Approx;

static ProcessSpec makeSpec(double sr=44100.0, int block=512) { return {sr,block,2}; }

TEST_CASE("TrackGainProcessor: default volume and pan", "[trackgain]") {
    TrackGainProcessor tg(0.8f, 0.f);
    REQUIRE(tg.getVolume() == Approx(0.8f));
    REQUIRE(tg.getPan()    == Approx(0.f));
    REQUIRE(!tg.isMuted());
}

TEST_CASE("TrackGainProcessor: setVolume clamps [0,1]", "[trackgain]") {
    TrackGainProcessor tg;
    tg.setVolume(2.f);
    REQUIRE(tg.getVolume() == Approx(1.f));
    tg.setVolume(-1.f);
    REQUIRE(tg.getVolume() == Approx(0.f));
}

TEST_CASE("TrackGainProcessor: setPan clamps [-1,+1]", "[trackgain]") {
    TrackGainProcessor tg;
    tg.setPan(5.f);
    REQUIRE(tg.getPan() == Approx(1.f));
    tg.setPan(-5.f);
    REQUIRE(tg.getPan() == Approx(-1.f));
}

TEST_CASE("TrackGainProcessor: mute produces silence", "[trackgain]") {
    TrackGainProcessor tg;
    tg.prepare(makeSpec());
    tg.setVolume(1.f);
    tg.setMuted(true);

    // Inject signal into inputBuf
    tg.inputBuf.setSize(2, 512, false, true, false);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill(tg.inputBuf.getWritePointer(ch), 1.f, 512);

    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    tg.process(ctx);
    REQUIRE(tg.outputBuf.getMagnitude(0, 0, 512) == Approx(0.f));
}

TEST_CASE("TrackGainProcessor: ParameterBank initialised after prepare", "[trackgain]") {
    TrackGainProcessor tg(0.8f, 0.f);
    tg.prepare(makeSpec());
    REQUIRE(tg.getNumParameters() == 3);
    REQUIRE(tg.getParameter(0) == Approx(0.8f)); // volume base
}

TEST_CASE("TrackGainProcessor: automation layer adds to volume without touching base", "[trackgain]") {
    TrackGainProcessor tg(0.5f, 0.f);
    tg.prepare(makeSpec());
    tg.setParameterAutomation(0, 0.2f);  // automation adds 0.2
    REQUIRE(tg.getParameter(0)     == Approx(0.5f));  // base unchanged
    REQUIRE(tg.resolvedParameter(0) == Approx(0.7f));  // 0.5 + 0.2
}

TEST_CASE("TrackGainProcessor: process produces non-zero output for non-zero input", "[trackgain]") {
    TrackGainProcessor tg(0.8f, 0.f);
    tg.prepare(makeSpec());
    tg.inputBuf.setSize(2, 512, false, true, false);
    for (int ch=0;ch<2;++ch)
        juce::FloatVectorOperations::fill(tg.inputBuf.getWritePointer(ch), 0.5f, 512);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    tg.process(ctx);
    REQUIRE(tg.outputBuf.getMagnitude(0,0,512) > 0.f);
}

TEST_CASE("TrackGainProcessor: parameter names correct", "[trackgain]") {
    TrackGainProcessor tg;
    REQUIRE(tg.getParameterName(0).isNotEmpty());
    REQUIRE(tg.getParameterName(1).isNotEmpty());
    REQUIRE(tg.getParameterName(2).isNotEmpty());
}

TEST_CASE("TrackGainProcessor: setVolume syncs to bank base", "[trackgain]") {
    TrackGainProcessor tg(0.5f, 0.f);
    tg.prepare(makeSpec());
    tg.setVolume(0.9f);
    REQUIRE(tg.getParameter(0) == Approx(0.9f));
}
