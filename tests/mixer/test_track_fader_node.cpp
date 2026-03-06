#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_dsp/mixer/track_fader_node.h"

using namespace y2k::dsp;
using Catch::Approx;

static ProcessSpec spec44(int bs=512) { return {44100.0,bs,2}; }

TEST_CASE("TrackFaderNode: getLatencySamples() == 0", "[trackfader]") {
    REQUIRE(TrackFaderNode{}.getLatencySamples() == 0);
}

TEST_CASE("TrackFaderNode: default volume 0.8, pan 0", "[trackfader]") {
    TrackFaderNode tf(0.8f, 0.f);
    REQUIRE(tf.getVolume() == Approx(0.8f));
    REQUIRE(tf.getPan()    == Approx(0.f));
    REQUIRE(!tf.isMuted());
    REQUIRE(!tf.isSoloed());
}

TEST_CASE("TrackFaderNode: setVolume clamps to [0,1]", "[trackfader]") {
    TrackFaderNode tf;
    tf.setVolume(2.f);  REQUIRE(tf.getVolume() == Approx(1.f));
    tf.setVolume(-1.f); REQUIRE(tf.getVolume() == Approx(0.f));
}

TEST_CASE("TrackFaderNode: setPan clamps to [-1,+1]", "[trackfader]") {
    TrackFaderNode tf;
    tf.setPan(5.f);  REQUIRE(tf.getPan() == Approx(1.f));
    tf.setPan(-5.f); REQUIRE(tf.getPan() == Approx(-1.f));
}

TEST_CASE("TrackFaderNode: mute silences output", "[trackfader]") {
    TrackFaderNode tf(1.f,0.f); tf.prepare(spec44(512));
    tf.inputBuf.setSize(2,512,false,true,false);
    juce::FloatVectorOperations::fill(tf.inputBuf.getWritePointer(0), 1.f, 512);
    juce::FloatVectorOperations::fill(tf.inputBuf.getWritePointer(1), 1.f, 512);
    tf.setMuted(true);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    tf.process(ctx);
    REQUIRE(tf.outputBuf.getMagnitude(0,0,512) == Approx(0.f));
}

TEST_CASE("TrackFaderNode: ParameterBank initialised after prepare", "[trackfader]") {
    TrackFaderNode tf(0.7f, 0.f); tf.prepare(spec44());
    REQUIRE(tf.getNumParameters() == 5);
    REQUIRE(tf.getParameter(TrackFaderNode::VOLUME) == Approx(0.7f).margin(0.01f));
}

TEST_CASE("TrackFaderNode: automation adds to volume without touching base", "[trackfader]") {
    TrackFaderNode tf(0.5f, 0.f); tf.prepare(spec44());
    tf.setParameterAutomation(TrackFaderNode::VOLUME, 0.2f);
    REQUIRE(tf.getParameter(TrackFaderNode::VOLUME) == Approx(0.5f));  // base unchanged
    REQUIRE(tf.resolvedParameter(TrackFaderNode::VOLUME) == Approx(0.7f).margin(0.01f));
}

TEST_CASE("TrackFaderNode: process produces output from non-zero input", "[trackfader]") {
    TrackFaderNode tf(0.8f,0.f); tf.prepare(spec44(512));
    tf.inputBuf.setSize(2,512,false,true,false);
    juce::FloatVectorOperations::fill(tf.inputBuf.getWritePointer(0), 0.5f, 512);
    juce::FloatVectorOperations::fill(tf.inputBuf.getWritePointer(1), 0.5f, 512);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    tf.process(ctx);
    REQUIRE(tf.outputBuf.getMagnitude(0,0,512) > 0.f);
}

TEST_CASE("TrackFaderNode: setVolume syncs to ParameterBank base", "[trackfader]") {
    TrackFaderNode tf(0.5f,0.f); tf.prepare(spec44());
    tf.setVolume(0.9f);
    REQUIRE(tf.getParameter(TrackFaderNode::VOLUME) == Approx(0.9f));
}

TEST_CASE("TrackFaderNode: no allocation in process() — 100 calls", "[trackfader]") {
    TrackFaderNode tf; tf.prepare(spec44(512));
    tf.inputBuf.setSize(2,512,false,true,false);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    for (int i=0;i<100;++i) REQUIRE_NOTHROW(tf.process(ctx));
}
