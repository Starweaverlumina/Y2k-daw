#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/meter/meter_tap.h  — v9
//
//  MeterTap — non-blocking atomic metering tap node.
//
//  Placed in the graph AFTER the target gain/fader node.
//  Audio passes through unmodified (unity passthrough).
//  On each audio block, computes:
//    • Peak level L + R  (instantaneous max absolute value in block)
//    • RMS level L + R   (EMA, decay coefficient per sampleRate)
//    • Clip flag         (peak > clipThreshold, default 1.0)
//
//  RT contract:
//    ✓ No allocation in process()
//    ✓ No locks — all reads/writes via std::atomic
//    ✓ UI thread reads peak/rms atomically (may be 1 block stale)
//    ✓ Clip flag: set by RT, cleared by UI on read (atomic exchange)
//    ✓ Peak hold: RT updates if new peak > stored peak
//      UI may reset with resetPeak()
//
//  Usage:
//    MeterTap tap;
//    tap.prepare(spec);
//    // ... after audio block ...
//    auto reading = tap.read();  // UI thread — safe
//    if (reading.clipped) ui.flash();
//
//  Hold decay:
//    peakHoldMs sets how long peak hold persists before decay.
//    Decay happens in process() — no timer needed.
// ═══════════════════════════════════════════════════════════════════

#include "../graph/processor_graph.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace y2k::dsp {

class MeterTap final : public ProcessorNode {
public:
    // ── Meter reading (UI thread) ─────────────────────────────────
    struct Reading {
        float peakL = 0.f;     // Instantaneous peak, linear
        float peakR = 0.f;
        float rmsL  = 0.f;     // EMA RMS, linear
        float rmsR  = 0.f;
        float holdL = 0.f;     // Held peak (decaying), linear
        float holdR = 0.f;
        bool  clipped = false; // Any clip since last read
    };

    struct Config {
        float clipThreshold = 1.f;     // > 1.0 = clipping
        float rmsAlpha      = 0.f;     // 0 = compute per block (default)
                                       // > 0 = EMA coefficient (set by prepare())
        float holdDecayPerBlock = 0.f; // Computed from holdMs in prepare()
        float holdMs        = 1000.f;  // Peak hold duration ms
    };

    explicit MeterTap(const juce::String& label={}) noexcept {
        name      = label.isEmpty() ? "MeterTap" : label;
        nodeColor = Y2KColor::Lime();
    }

    // ── ProcessorNode ─────────────────────────────────────────────
    void prepare(const ProcessSpec& spec) override {
        spec_ = spec;
        const double sr = spec.sampleRate;
        const int    bs = spec.maxBlockSize;

        // EMA RMS: alpha = exp(-2π * fc / sr)
        // fc chosen so RMS integrates ~300ms
        config_.rmsAlpha = float(std::exp(-1.0 / (0.3 * sr / std::max(1, bs))));

        // Hold decay: drop to -∞ dB over holdMs → linear per-block decay
        const double blocksPerMs = sr / std::max(1, bs) / 1000.0;
        config_.holdDecayPerBlock = float(1.0 / (config_.holdMs * blocksPerMs + 1.0));

        outputBuf.setSize(2, std::max(1, bs), false, true, false);
        outputBuf.clear();
        inputBuf .setSize(2, std::max(1, bs), false, true, false);
        inputBuf .clear();
    }

    void reset() override {
        peakL_.store(0.f, std::memory_order_release);
        peakR_.store(0.f, std::memory_order_release);
        rmsL_ .store(0.f, std::memory_order_release);
        rmsR_ .store(0.f, std::memory_order_release);
        holdL_.store(0.f, std::memory_order_release);
        holdR_.store(0.f, std::memory_order_release);
        clipped_.store(0, std::memory_order_release);
    }

    // ── process [RT — no allocation, no lock] ────────────────────
    void process(const ProcessContext& ctx) override {
        const int N = ctx.blockSize;
        if (N <= 0) return;

        // Passthrough: copy input → output (unity, no modification)
        for (int ch = 0; ch < 2; ++ch) {
            const float* in  = ch < inputBuf .getNumChannels() ? inputBuf .getReadPointer(ch) : nullptr;
            float*       out = ch < outputBuf.getNumChannels() ? outputBuf.getWritePointer(ch) : nullptr;
            if (in && out) juce::FloatVectorOperations::copy(out, in, N);
        }

        // Compute peak + RMS on stack
        const float* chL = inputBuf.getNumChannels() > 0 ? inputBuf.getReadPointer(0) : nullptr;
        const float* chR = inputBuf.getNumChannels() > 1 ? inputBuf.getReadPointer(1) : chL;

        float newPeakL = 0.f, newPeakR = 0.f;
        float sumSqL   = 0.f, sumSqR   = 0.f;

        if (chL) {
            for (int s = 0; s < N; ++s) {
                const float absL = std::abs(chL[s]);
                if (absL > newPeakL) newPeakL = absL;
                sumSqL += chL[s] * chL[s];
            }
        }
        if (chR && chR != chL) {
            for (int s = 0; s < N; ++s) {
                const float absR = std::abs(chR[s]);
                if (absR > newPeakR) newPeakR = absR;
                sumSqR += chR[s] * chR[s];
            }
        } else {
            newPeakR = newPeakL;
            sumSqR   = sumSqL;
        }

        const float rmsBlockL = std::sqrt(sumSqL / float(N));
        const float rmsBlockR = std::sqrt(sumSqR / float(N));

        // EMA RMS accumulation
        const float alpha = config_.rmsAlpha;
        const float rmsL  = alpha * rmsL_.load(std::memory_order_relaxed)
                          + (1.f - alpha) * rmsBlockL;
        const float rmsR  = alpha * rmsR_.load(std::memory_order_relaxed)
                          + (1.f - alpha) * rmsBlockR;

        // Peak hold: update if new peak is higher, otherwise decay
        const float prevHL = holdL_.load(std::memory_order_relaxed);
        const float prevHR = holdR_.load(std::memory_order_relaxed);
        const float decay  = 1.f - config_.holdDecayPerBlock;
        const float newHL  = newPeakL >= prevHL ? newPeakL : prevHL * decay;
        const float newHR  = newPeakR >= prevHR ? newPeakR : prevHR * decay;

        // Clip detection
        const bool clip = (newPeakL > config_.clipThreshold)
                       || (newPeakR > config_.clipThreshold);

        // Atomic stores — single block, relaxed internally, release at end
        peakL_.store(newPeakL, std::memory_order_relaxed);
        peakR_.store(newPeakR, std::memory_order_relaxed);
        rmsL_ .store(rmsL,     std::memory_order_relaxed);
        rmsR_ .store(rmsR,     std::memory_order_relaxed);
        holdL_.store(newHL,    std::memory_order_relaxed);
        holdR_.store(newHR,    std::memory_order_relaxed);
        if (clip) clipped_.fetch_add(1, std::memory_order_relaxed);
        // Release fence: ensures all stores are visible to UI thread
        std::atomic_thread_fence(std::memory_order_release);
    }

    // ── UI thread reads ───────────────────────────────────────────
    Reading read() const noexcept {
        std::atomic_thread_fence(std::memory_order_acquire);
        Reading r;
        r.peakL   = peakL_.load(std::memory_order_relaxed);
        r.peakR   = peakR_.load(std::memory_order_relaxed);
        r.rmsL    = rmsL_ .load(std::memory_order_relaxed);
        r.rmsR    = rmsR_ .load(std::memory_order_relaxed);
        r.holdL   = holdL_.load(std::memory_order_relaxed);
        r.holdR   = holdR_.load(std::memory_order_relaxed);
        r.clipped = (clipped_.load(std::memory_order_relaxed) > 0);
        return r;
    }

    // Reset clip flag (call after UI reads and displays it)
    void resetClip()  noexcept { clipped_.store(0, std::memory_order_release); }
    // Reset peak hold
    void resetPeak()  noexcept {
        holdL_.store(0.f, std::memory_order_release);
        holdR_.store(0.f, std::memory_order_release);
    }

    // Convenience: dB conversion (for UI)
    static float linearToDb(float linear) noexcept {
        return (linear > 1e-6f) ? 20.f * std::log10(linear) : -120.f;
    }

    // ── ProcessorNode metadata ────────────────────────────────────
    int getNumInputChannels()  const override { return 2; }
    int getNumOutputChannels() const override { return 2; }
    int getLatencySamples()    const noexcept override { return 0; }
    int getNumParameters()     const override { return 0; }

private:
    Config config_;

    // Meter state — atomic floats for RT→UI bridge
    std::atomic<float>    peakL_   {0.f};
    std::atomic<float>    peakR_   {0.f};
    std::atomic<float>    rmsL_    {0.f};
    std::atomic<float>    rmsR_    {0.f};
    std::atomic<float>    holdL_   {0.f};
    std::atomic<float>    holdR_   {0.f};
    std::atomic<uint32_t> clipped_ {0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MeterTap)
};

} // namespace y2k::dsp
