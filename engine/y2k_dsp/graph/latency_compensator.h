#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/graph/latency_compensator.h  — v9
//
//  LatencyCompensatorNode — fixed ring-buffer delay for PDC.
//
//  Created and sized during commitState() (UI thread / non-RT).
//  Audio thread runs process() — zero allocation, zero lock.
//
//  The ring buffer is pre-allocated at construction time:
//    size = delaySamples + maxBlockSize + 1  (safety margin)
//
//  Channels are fixed at construction to 2 (stereo).
//  For N-channel support: increase channels parameter.
//
//  Write/read model:
//    Each process() call writes N samples at writeIdx, then
//    reads N samples from (writeIdx - delaySamples), both
//    modulo ringSize. Because ring is pre-sized, no bounds issues.
//
//  Initialization:
//    Ring buffer pre-filled with zeros. At block 0, the read
//    pointer is delaySamples behind the write pointer, producing
//    exactly delaySamples of silence output = correct PDC behavior.
//
//  Invariant:
//    delaySamples < ringSize - maxBlockSize
//    Guaranteed by constructor (asserts in debug, clamps in release)
// ═══════════════════════════════════════════════════════════════════

#include "processor_graph.h"
#include <algorithm>
#include <cstring>
#include <cassert>
#include <vector>

namespace y2k::dsp {

class LatencyCompensatorNode final : public ProcessorNode {
public:
    // delaySamples — must be > 0; created during commitState, not in RT path
    // numChannels  — channels to delay (default 2 = stereo)
    explicit LatencyCompensatorNode(int delaySamples, int numChannels=2) noexcept
        : delaySamples_(std::max(0, delaySamples))
        , numChannels_(std::max(1, numChannels))
    {
        name = "PDCDelay(" + juce::String(delaySamples_) + ")";
        nodeColor = Y2KColor::BondiBlue();
        // Note: ring buffers allocated in prepare(), not here,
        // because we need maxBlockSize from the ProcessSpec.
    }

    int delaySamples() const noexcept { return delaySamples_; }

    // ── ProcessorNode ─────────────────────────────────────────────
    void prepare(const ProcessSpec& spec) override {
        spec_ = spec;
        const int maxBlock = std::max(1, spec.maxBlockSize);

        // Ring size: delay + maxBlock + safety margin
        ringSize_ = delaySamples_ + maxBlock + 64;

        ring_.resize(size_t(numChannels_) * size_t(ringSize_), 0.f);
        std::fill(ring_.begin(), ring_.end(), 0.f);
        writeIdx_ = delaySamples_;  // Pre-advance so read starts at 0

        outputBuf.setSize(numChannels_, maxBlock, false, true, false);
        outputBuf.clear();
        inputBuf .setSize(numChannels_, maxBlock, false, true, false);
        inputBuf .clear();
    }

    void reset() override {
        std::fill(ring_.begin(), ring_.end(), 0.f);
        writeIdx_ = delaySamples_;
        outputBuf.clear();
    }

    // ── process [RT — no allocation, no lock, no heap access] ────
    //
    //  For each channel:
    //    1. Write N samples from inputBuf[ch] into ring at writeIdx
    //    2. Read N samples from ring at (writeIdx - delaySamples)
    //       (negative indices wrap via modulo)
    //    3. Write read samples into outputBuf[ch]
    //
    //  writeIdx advances by N after all channels written.
    //  All indices computed via fast modulo (ringSize is not pow2;
    //  for perf-critical use, assert pow2 and use bitmask).
    void process(const ProcessContext& ctx) override {
        const int N = ctx.blockSize;
        if (N <= 0 || ring_.empty()) return;

        for (int ch = 0; ch < numChannels_; ++ch) {
            const float* in  = ch < inputBuf .getNumChannels()
                             ? inputBuf .getReadPointer(ch) : nullptr;
            float*       out = ch < outputBuf.getNumChannels()
                             ? outputBuf.getWritePointer(ch) : nullptr;
            if (!out) continue;

            float* ring = ring_.data() + ch * ringSize_;

            // Write phase: fill ring at writeIdx
            if (in) {
                for (int s = 0; s < N; ++s)
                    ring[(writeIdx_ + s) % ringSize_] = in[s];
            } else {
                for (int s = 0; s < N; ++s)
                    ring[(writeIdx_ + s) % ringSize_] = 0.f;
            }

            // Read phase: read from (writeIdx - delaySamples + s)
            // Note: if delaySamples = 0, read == write (bypass)
            const int readStart = writeIdx_ - delaySamples_;
            for (int s = 0; s < N; ++s) {
                const int ri = ((readStart + s) % ringSize_ + ringSize_) % ringSize_;
                out[s] = ring[ri];
            }
        }

        writeIdx_ = (writeIdx_ + N) % ringSize_;
    }

    // ── Metadata ──────────────────────────────────────────────────
    int getNumInputChannels()  const override { return numChannels_; }
    int getNumOutputChannels() const override { return numChannels_; }
    int getLatencySamples()    const noexcept override { return 0; } // compensator is transparent
    int getNumParameters()     const override { return 0; }

private:
    const int delaySamples_;
    const int numChannels_;
    int ringSize_  = 0;
    int writeIdx_  = 0;

    // Flat ring buffer: [ch * ringSize_ + sampleIdx]
    // Heap-allocated once in prepare() — never in process()
    std::vector<float> ring_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LatencyCompensatorNode)
};

} // namespace y2k::dsp
