// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/recording/audio_recorder.cpp
// ═══════════════════════════════════════════════════════════════════

#include "audio_recorder.h"

#include <algorithm>
#include <cstring>

namespace y2k::dsp {

AudioRecorder::AudioRecorder(int numChannels, int ringSeconds)
    : numChannels_(numChannels), ringSeconds_(ringSeconds)
{}

AudioRecorder::~AudioRecorder() {
    disarm();
}

// ─────────────────────────────────────────────────────────────────
//  arm  [non-RT]
//  Pre-allocates ring buffer. Must be called before startCapture.
// ─────────────────────────────────────────────────────────────────
void AudioRecorder::arm(double sampleRate, int maxBlockSize) {
    sampleRate_   = sampleRate;
    maxBlockSize_ = maxBlockSize;

    const int capacity = static_cast<int>(sampleRate * ringSeconds_) + maxBlockSize + 1;
    ringL_.assign(capacity, 0.f);
    ringR_.assign(capacity, 0.f);
    writePos_.store(0, std::memory_order_relaxed);
    readPos_ .store(0, std::memory_order_relaxed);
    samplesWritten_.store(0, std::memory_order_relaxed);
    armed_ = true;
}

void AudioRecorder::disarm() {
    recording_.store(false, std::memory_order_release);
    armed_ = false;
    ringL_.clear();
    ringR_.clear();
}

// ─────────────────────────────────────────────────────────────────
//  startCapture / stopCapture  [non-RT]
// ─────────────────────────────────────────────────────────────────
void AudioRecorder::startCapture(double startBeat) {
    if (!armed_) return;
    startBeat_ = startBeat;
    writePos_.store(0, std::memory_order_relaxed);
    readPos_ .store(0, std::memory_order_relaxed);
    samplesWritten_.store(0, std::memory_order_relaxed);
    recording_.store(true, std::memory_order_release);
}

void AudioRecorder::stopCapture() {
    recording_.store(false, std::memory_order_release);
}

double AudioRecorder::getRecordedSecs() const noexcept {
    return samplesWritten_.load(std::memory_order_acquire) / sampleRate_;
}

// ─────────────────────────────────────────────────────────────────
//  writeBlock  [RT]
//
//  SPSC ring: writePos_ advanced by RT writer only.
//             readPos_  advanced by non-RT reader only.
//  No CAS, no mutex — pure atomic load/store with release/acquire.
// ─────────────────────────────────────────────────────────────────
void AudioRecorder::writeBlock(const float* const* inputChannelData,
                               int                 numInputChannels,
                               int                 numSamples) noexcept
{
    if (!recording_.load(std::memory_order_acquire)) return;
    if (!inputChannelData || numSamples <= 0) return;

    const int cap  = ringCapacity();
    int       wp   = writePos_.load(std::memory_order_relaxed);
    const int rp   = readPos_ .load(std::memory_order_acquire);

    const float* chL = numInputChannels > 0 ? inputChannelData[0] : nullptr;
    const float* chR = numInputChannels > 1 ? inputChannelData[1] : chL;

    for (int s = 0; s < numSamples; ++s) {
        // Check available space (leave 1 slot gap to distinguish full/empty)
        const int next = (wp + 1) % cap;
        if (next == rp) break;  // Ring full — drop sample, never block

        ringL_[wp] = chL ? chL[s] : 0.f;
        ringR_[wp] = chR ? chR[s] : 0.f;
        wp = next;
    }

    writePos_.store(wp, std::memory_order_release);
    samplesWritten_.fetch_add(numSamples, std::memory_order_relaxed);
}

// ─────────────────────────────────────────────────────────────────
//  flushToDisk  [non-RT]
//
//  Drains the ring buffer into a 24-bit stereo WAV file.
//  Safe to call from a background thread after stopCapture().
// ─────────────────────────────────────────────────────────────────
juce::File AudioRecorder::flushToDisk(const juce::File& destDir,
                                      const juce::String& name)
{
    destDir.createDirectory();

    const juce::File outFile = destDir.getChildFile(
        name + "_" + juce::String(juce::Time::currentTimeMillis()) + ".wav");

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormat> fmt(new juce::WavAudioFormat());
    std::unique_ptr<juce::FileOutputStream> fos(outFile.createOutputStream());
    if (!fos) return {};

    // 24-bit stereo WAV
    auto* writer = fmt->createWriterFor(fos.release(),
                                        sampleRate_,
                                        2,      // channels
                                        24,     // bits
                                        {},
                                        0);
    if (!writer) return {};

    std::unique_ptr<juce::AudioFormatWriter> writerPtr(writer);

    // Drain ring buffer
    const int cap = ringCapacity();
    int rp = readPos_.load(std::memory_order_acquire);
    int wp = writePos_.load(std::memory_order_acquire);

    constexpr int kChunkSize = 4096;
    juce::AudioBuffer<float> chunk(2, kChunkSize);

    while (rp != wp) {
        int count = 0;
        float* outL = chunk.getWritePointer(0);
        float* outR = chunk.getWritePointer(1);

        while (rp != wp && count < kChunkSize) {
            outL[count] = ringL_[rp];
            outR[count] = ringR_[rp];
            rp = (rp + 1) % cap;
            ++count;
        }

        if (count > 0)
            writerPtr->writeFromAudioSampleBuffer(chunk, 0, count);
    }

    readPos_.store(rp, std::memory_order_release);
    writerPtr->flush();

    if (onComplete_) onComplete_(outFile);
    return outFile;
}

} // namespace y2k::dsp
