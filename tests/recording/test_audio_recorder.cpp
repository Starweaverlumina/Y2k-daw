#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_dsp/recording/audio_recorder.h"

using namespace y2k::dsp;
using Catch::Approx;

TEST_CASE("AudioRecorder: arm allocates ring buffer", "[recorder]") {
    AudioRecorder rec;
    rec.arm(44100.0, 512);
    REQUIRE(rec.isArmed());
    REQUIRE(!rec.isRecording());
}

TEST_CASE("AudioRecorder: startCapture enters recording state", "[recorder]") {
    AudioRecorder rec;
    rec.arm(44100.0, 512);
    rec.startCapture(0.0);
    REQUIRE(rec.isRecording());
    REQUIRE(rec.getStartBeat() == Approx(0.0));
    rec.stopCapture();
    REQUIRE(!rec.isRecording());
}

TEST_CASE("AudioRecorder: writeBlock before arm is safe", "[recorder]") {
    AudioRecorder rec;
    const float data[64] = {};
    const float* ch[2] = { data, data };
    // Should not crash — not recording, not armed
    rec.writeBlock(ch, 2, 64);
    SUCCEED();
}

TEST_CASE("AudioRecorder: writeBlock captures samples", "[recorder]") {
    AudioRecorder rec;
    rec.arm(44100.0, 64);
    rec.startCapture(0.0);

    std::vector<float> buf(64, 0.5f);
    const float* ch[2] = { buf.data(), buf.data() };
    rec.writeBlock(ch, 2, 64);

    REQUIRE(rec.getRecordedSecs() > 0.0);
    rec.stopCapture();
}

TEST_CASE("AudioRecorder: flushToDisk writes WAV file", "[recorder]") {
    AudioRecorder rec;
    rec.arm(44100.0, 256);
    rec.startCapture(0.0);

    std::vector<float> buf(256, 0.25f);
    const float* ch[2] = { buf.data(), buf.data() };
    for (int i = 0; i < 10; ++i)
        rec.writeBlock(ch, 2, 256);

    rec.stopCapture();

    const juce::File tmpDir = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("y2k_rec_test");
    tmpDir.createDirectory();
    const juce::File wav = rec.flushToDisk(tmpDir, "test_rec");
    REQUIRE(wav.existsAsFile());
    REQUIRE(wav.getSize() > 0);
    tmpDir.deleteRecursively();
}

TEST_CASE("AudioRecorder: disarm resets state", "[recorder]") {
    AudioRecorder rec;
    rec.arm(44100.0, 512);
    rec.startCapture(0.0);
    rec.disarm();
    REQUIRE(!rec.isArmed());
    REQUIRE(!rec.isRecording());
}
