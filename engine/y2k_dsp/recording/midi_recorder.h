#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/recording/midi_recorder.h
//
//  MidiRecorder — captures live MIDI input into a MidiClip.
//
//  Threading contract:
//    arm() / disarm() / startCapture() / stopCapture() — UI thread
//    processBlock()                                     — RT audio thread
//    buildClip()                                        — non-RT after stop
//
//  Design:
//    During recording, noteOn events are stored with their beat
//    position. Corresponding noteOff events close out the note
//    duration. Orphaned noteOns (held when recording stops) are
//    given a 1-beat duration fallback.
//
//    Uses a fixed-capacity event buffer allocated in arm() so
//    processBlock() never allocates in the RT thread.
// ═══════════════════════════════════════════════════════════════════

#include "../graph/processor_graph.h"
#include "../../y2k_engine/session.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <vector>

namespace y2k::dsp {

class MidiRecorder {
public:
    explicit MidiRecorder(int maxEvents = 16384);
    ~MidiRecorder() = default;

    // ── Non-RT ────────────────────────────────────────────────────
    void arm  (double sampleRate);
    void disarm();
    bool isArmed()     const noexcept { return armed_; }
    bool isRecording() const noexcept { return recording_.load(std::memory_order_acquire); }

    void startCapture(double startBeat, double bpm);
    void stopCapture();

    // Builds a MidiClip from captured events.
    // Call after stopCapture() from a non-RT thread.
    std::unique_ptr<y2k::engine::MidiClip> buildClip(
        double clipStartBeat,
        const juce::String& name = "Recorded MIDI") const;

    // ── RT: call from audio callback ──────────────────────────────
    void processBlock(const juce::MidiBuffer& midi,
                      double                  ppqStart,
                      double                  bpm,
                      int                     numSamples,
                      double                  sampleRate) noexcept;

private:
    struct RawEvent {
        double beat;
        int    note;
        float  velocity;  // 0 = note-off
        int    channel;
    };

    int    maxEvents_;
    double sampleRate_ = 44100.0;
    double startBeat_  = 0.0;
    double bpm_        = 120.0;
    bool   armed_      = false;

    std::atomic<bool> recording_{false};
    std::atomic<int>  eventCount_{0};

    // Pre-allocated; filled by RT, read by non-RT after stop
    std::vector<RawEvent> events_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiRecorder)
};

} // namespace y2k::dsp
