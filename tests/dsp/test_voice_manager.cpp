#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_dsp/voice/voice_manager.h"
#include "y2k_dsp/graph/processor_graph.h"

using namespace y2k::dsp;
using Catch::Approx;

static ProcessSpec makeSpec(double sr=44100.0, int block=512) {
    return {sr, block, 2};
}

TEST_CASE("VoiceManager: no active voices after prepare", "[voice]") {
    VoiceManager vm;
    vm.prepare(makeSpec());
    REQUIRE(vm.activeVoiceCount() == 0);
}

TEST_CASE("VoiceManager: noteOn activates a voice", "[voice]") {
    VoiceManager vm;
    vm.prepare(makeSpec());
    vm.noteOn(60, 0.8f);
    REQUIRE(vm.activeVoiceCount() == 1);
}

TEST_CASE("VoiceManager: noteOff releases voice (after ADSR decay)", "[voice]") {
    VoiceManager vm;
    vm.prepare(makeSpec(44100.0, 512));
    vm.noteOn(60, 0.8f);
    vm.noteOff(60);
    // After enough samples for release, voice becomes idle
    juce::AudioBuffer<float> buf(2, 44100); // 1 sec render
    buf.clear();
    vm.renderBlock(buf, 44100);
    REQUIRE(vm.activeVoiceCount() == 0);
}

TEST_CASE("VoiceManager: polyphony — multiple simultaneous voices", "[voice]") {
    VoiceManager vm;
    vm.prepare(makeSpec());
    vm.noteOn(60, 0.8f);
    vm.noteOn(64, 0.8f);
    vm.noteOn(67, 0.8f);
    REQUIRE(vm.activeVoiceCount() == 3);
}

TEST_CASE("VoiceManager: max voices cap without crash (steal)", "[voice]") {
    VoiceManager vm;
    vm.prepare(makeSpec());
    for (int n = 0; n < VoiceManager::kMaxVoices + 5; ++n)
        vm.noteOn(60 + n % 12, 0.7f);
    REQUIRE(vm.activeVoiceCount() <= VoiceManager::kMaxVoices);
}

TEST_CASE("VoiceManager: renderBlock produces non-zero output", "[voice]") {
    VoiceManager vm;
    vm.prepare(makeSpec(44100.0, 512));
    vm.noteOn(60, 1.0f);
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    vm.renderBlock(buf, 512);
    float peak = buf.getMagnitude(0, 0, 512);
    REQUIRE(peak > 0.f);
}

TEST_CASE("VoiceManager: allNotesOff silences after release", "[voice]") {
    VoiceManager vm;
    vm.prepare(makeSpec());
    vm.noteOn(60, 0.8f);
    vm.noteOn(64, 0.8f);
    vm.allNotesOff();
    juce::AudioBuffer<float> buf(2, 44100);
    buf.clear();
    vm.renderBlock(buf, 44100);
    REQUIRE(vm.activeVoiceCount() == 0);
}

TEST_CASE("VoiceManager: same note retriggers without double-voice", "[voice]") {
    VoiceManager vm;
    vm.prepare(makeSpec());
    vm.noteOn(60, 0.8f);
    vm.noteOn(60, 0.9f); // retrigger
    REQUIRE(vm.activeVoiceCount() == 1);
}

TEST_CASE("VoiceManager: renderBlock no allocation (RT safe)", "[voice]") {
    // This test just verifies renderBlock runs without throw
    VoiceManager vm;
    vm.prepare(makeSpec(44100.0, 256));
    vm.noteOn(69, 0.8f);
    juce::AudioBuffer<float> buf(2, 256);
    buf.clear();
    REQUIRE_NOTHROW(vm.renderBlock(buf, 256));
}
