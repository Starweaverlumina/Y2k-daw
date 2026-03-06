#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_dsp/recording/midi_recorder.h"

using namespace y2k::dsp;
using Catch::Approx;

static juce::MidiBuffer makeNoteOnOff(int note, int onSample, int offSample, float vel = 0.8f) {
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn (1, note, static_cast<uint8_t>(int(vel*127))), onSample);
    buf.addEvent(juce::MidiMessage::noteOff(1, note), offSample);
    return buf;
}

TEST_CASE("MidiRecorder: arm and start recording", "[midi_rec]") {
    MidiRecorder rec;
    rec.arm(44100.0);
    rec.startCapture(0.0, 120.0);
    REQUIRE(rec.isRecording());
    rec.stopCapture();
    REQUIRE(!rec.isRecording());
}

TEST_CASE("MidiRecorder: captures noteOn/Off and builds clip", "[midi_rec]") {
    MidiRecorder rec;
    rec.arm(44100.0);
    rec.startCapture(0.0, 120.0);

    // One note: on at sample 0, off at sample 22050 (0.5 sec = 1 beat at 120bpm)
    auto buf = makeNoteOnOff(60, 0, 22050);
    rec.processBlock(buf, 0.0, 120.0, 44100, 44100.0);
    rec.stopCapture();

    auto clip = rec.buildClip(0.0, "Test");
    REQUIRE(clip != nullptr);
    REQUIRE(!clip->notes.empty());
    REQUIRE(clip->notes[0].pitch == 60);
    REQUIRE(clip->notes[0].durationBeats == Approx(1.0).margin(0.1));
}

TEST_CASE("MidiRecorder: multiple notes build correct clip", "[midi_rec]") {
    MidiRecorder rec;
    rec.arm(44100.0);
    rec.startCapture(0.0, 120.0);

    juce::MidiBuffer buf;
    // C, E, G chord at beat 0
    buf.addEvent(juce::MidiMessage::noteOn (1, 60, (uint8_t)100), 0);
    buf.addEvent(juce::MidiMessage::noteOn (1, 64, (uint8_t)100), 0);
    buf.addEvent(juce::MidiMessage::noteOn (1, 67, (uint8_t)100), 0);
    buf.addEvent(juce::MidiMessage::noteOff(1, 60), 22050);
    buf.addEvent(juce::MidiMessage::noteOff(1, 64), 22050);
    buf.addEvent(juce::MidiMessage::noteOff(1, 67), 22050);

    rec.processBlock(buf, 0.0, 120.0, 44100, 44100.0);
    rec.stopCapture();

    auto clip = rec.buildClip(0.0, "Chord");
    REQUIRE(clip != nullptr);
    REQUIRE(clip->notes.size() == 3);
}

TEST_CASE("MidiRecorder: orphaned noteOn gets fallback duration", "[midi_rec]") {
    MidiRecorder rec;
    rec.arm(44100.0);
    rec.startCapture(0.0, 120.0);

    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (uint8_t)80), 0);
    // No note-off — simulate held key when recording stopped
    rec.processBlock(buf, 0.0, 120.0, 512, 44100.0);
    rec.stopCapture();

    auto clip = rec.buildClip(0.0, "Orphan");
    REQUIRE(clip != nullptr);
    REQUIRE(!clip->notes.empty());
    REQUIRE(clip->notes[0].durationBeats >= 1.0);
}

TEST_CASE("MidiRecorder: empty recording produces empty clip", "[midi_rec]") {
    MidiRecorder rec;
    rec.arm(44100.0);
    rec.startCapture(0.0, 120.0);
    rec.stopCapture();

    auto clip = rec.buildClip(0.0, "Empty");
    REQUIRE(clip != nullptr);
    REQUIRE(clip->notes.empty());
}

TEST_CASE("MidiRecorder: clip duration rounds to bar boundary", "[midi_rec]") {
    MidiRecorder rec;
    rec.arm(44100.0);
    rec.startCapture(0.0, 120.0);

    // One short note = ~0.5 beat
    auto buf = makeNoteOnOff(60, 0, 11025);
    rec.processBlock(buf, 0.0, 120.0, 44100, 44100.0);
    rec.stopCapture();

    auto clip = rec.buildClip(0.0, "Short");
    // Duration should round up to 4 beats (1 bar)
    REQUIRE(clip->durationBeats >= 4.0);
    REQUIRE(static_cast<int>(clip->durationBeats) % 4 == 0);
}
