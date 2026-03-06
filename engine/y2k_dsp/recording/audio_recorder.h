#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/recording/audio_recorder.h
//
//  AudioRecorder — captures live audio from the device input into
//  a juce::AudioBuffer that is later written to disk as a WAV file
//  and wrapped into an AudioClip in the session.
//
//  Threading contract:
//    arm() / disarm()         — UI thread
//    startCapture()           — UI thread (called just before play)
//    stopCapture()            — UI thread
//    writeBlock()             — RT audio thread
//    flushToDisk()            — non-RT (call after stopCapture)
//
//  RT contract (writeBlock):
//    No allocation — ring buffer is pre-allocated in arm().
//    No locks      — SPSC ring buffer with atomic indices.
//    No disk I/O   — writing happens in flushToDisk() off the RT thread.
//
//  Buffer sizing:
//    kRingSeconds = 60 seconds at 48kHz stereo = ~23 MB max.
//    Adjust for your memory budget.
// ═══════════════════════════════════════════════════════════════════

#include "../graph/processor_graph.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <vector>

namespace y2k::dsp {

class AudioRecorder {
public:
    // Called after flushToDisk() completes — receives the written WAV file
    using RecordingCompleteCallback = std::function<void(const juce::File&)>;

    explicit AudioRecorder(int numChannels = 2,
                           int ringSeconds = 60);
    ~AudioRecorder();

    // ── Non-RT setup ─────────────────────────────────────────────
    void arm  (double sampleRate, int maxBlockSize);
    void disarm();
    bool isArmed()     const noexcept { return armed_;             }
    bool isRecording() const noexcept { return recording_.load(std::memory_order_acquire); }

    // Start / stop
    void startCapture(double startBeat = 0.0);
    void stopCapture();

    // ── RT: called from audio callback ────────────────────────────
    // Writes numSamples from inputChannelData into the ring buffer.
    // Silently drops samples if ring is full (never blocks).
    void writeBlock(const float* const* inputChannelData,
                    int                 numInputChannels,
                    int                 numSamples) noexcept;

    // ── Non-RT: flush ring buffer to WAV file ─────────────────────
    // destDir  — directory to write into (e.g. bundle/audio/)
    // name     — base filename without extension
    // Returns  — the written .wav file
    juce::File flushToDisk(const juce::File& destDir,
                           const juce::String& name);

    void setOnComplete(RecordingCompleteCallback cb) { onComplete_ = std::move(cb); }

    double getStartBeat()    const noexcept { return startBeat_; }
    double getRecordedSecs() const noexcept;

private:
    static constexpr int kChannels = 2;

    int    numChannels_  = 2;
    int    ringSeconds_  = 60;
    double sampleRate_   = 44100.0;
    int    maxBlockSize_ = 512;

    bool   armed_ = false;
    double startBeat_ = 0.0;

    // SPSC ring buffer — pre-allocated in arm()
    std::vector<float>         ringL_;
    std::vector<float>         ringR_;
    std::atomic<int>           writePos_{0};
    std::atomic<int>           readPos_ {0};
    std::atomic<bool>          recording_{false};
    std::atomic<int>           samplesWritten_{0};

    RecordingCompleteCallback  onComplete_;

    int ringCapacity() const noexcept { return static_cast<int>(ringL_.size()); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRecorder)
};

} // namespace y2k::dsp
