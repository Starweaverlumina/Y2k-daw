#pragma once

// ═══════════════════════════════════════════════════════════════════
//  Y2K DSP — "Grape Compressor"
//  Opto-style smooth compressor with VU meter output.
//  Transparent "glue" character at moderate settings.
//  Part of the Y2K Suite built-in effects.
// ═══════════════════════════════════════════════════════════════════

#include "../graph/processor_graph.h"
#include <atomic>
#include <cmath>

namespace y2k::dsp {

// ─────────────────────────────────────────────────────────────────
//  GrapeCompressor::Parameters  (all 0-100 for Y2K UI)
// ─────────────────────────────────────────────────────────────────
struct GrapeParams {
    // Y2K UI labels: "Gentle" | "Firm" | "Aggressive" mapped to ratio
    float threshold  = 60.f;   // 0→-60dB, 100→0dB
    float ratio      = 30.f;   // 0→1:1 (off), 50→4:1, 100→∞:1 (limit)
    float attack     = 40.f;   // 0→0.1ms, 100→100ms (log)
    float release    = 50.f;   // 0→10ms, 100→3s (log)
    float makeupGain = 50.f;   // 0→off (auto), 50→0dB, 100→+18dB
    bool  autoMakeup = true;   // Auto makeup when makeupGain=0
    bool  sidechain  = false;  // External sidechain input (channel 2)
    float sideHPF    = 0.f;    // Sidechain high-pass (0→off, 100→500Hz)
    float mix        = 100.f;  // Parallel compression blend (0=dry, 100=wet)
};

// ─────────────────────────────────────────────────────────────────
//  GrapeCompressor
// ─────────────────────────────────────────────────────────────────
class GrapeCompressor : public ProcessorNode {
public:
    GrapeCompressor();

    void prepare(const ProcessSpec& spec) override;
    void process(const ProcessContext& ctx) override;
    void reset() override;

    int getNumInputChannels()  const override { return 2; }
    int getNumOutputChannels() const override { return 2; }
    int getNumParameters()     const override { return P_NUM_PARAMS; }

    float        getParameter(int idx) const override;
    void         setParameter(int idx, float value) override;
    juce::String getParameterName(int idx) const override;

    // ── Real-time metering (read from UI thread) ─────────────────
    // Safe to call from any thread after prepare()
    float getGainReductionDb()   const noexcept { return gainReductionDb_.load(std::memory_order_relaxed); }
    float getInputLevelDb()      const noexcept { return inputLevelDb_.load(std::memory_order_relaxed);    }
    float getOutputLevelDb()     const noexcept { return outputLevelDb_.load(std::memory_order_relaxed);   }

    GrapeParams& params() { return params_; }

private:
    // ── Level detection ──────────────────────────────────────────
    float computeLevelDb(const juce::AudioBuffer<float>& buf, int numSamples) const noexcept;

    // ── Gain computation (opto characteristic) ───────────────────
    float computeGainDb(float levelDb, float threshDb, float ratio) const noexcept;

    // ── Parameter mapping helpers ─────────────────────────────────
    void applyParameters();
    float thresholdDb()  const noexcept;  // → -60 to 0 dB
    float ratio()        const noexcept;  // → 1.0 to ∞
    float attackMs()     const noexcept;  // → 0.1 to 100 ms
    float releaseMs()    const noexcept;  // → 10 to 3000 ms
    float makeupDb()     const noexcept;  // → 0 to +18 dB

    // ── State ─────────────────────────────────────────────────────
    GrapeParams  params_;
    ProcessSpec  spec_;

    // Envelope follower state
    float envL_     = 0.f;
    float envR_     = 0.f;
    float attackCoeff_  = 0.f;
    float releaseCoeff_ = 0.f;
    float currentGainDb_ = 0.f;   // Current gain reduction (dB, ≤0)

    // Smoothed makeup
    SmoothParameter smoothMakeup_;

    // Sidechain HPF state
    float hpfStateL_[2] = {0.f, 0.f};
    float hpfStateR_[2] = {0.f, 0.f};

    // Metering — written on audio thread, read on UI thread
    std::atomic<float> gainReductionDb_{0.f};
    std::atomic<float> inputLevelDb_{-96.f};
    std::atomic<float> outputLevelDb_{-96.f};

    // Meter ballistics
    float peakInputL_  = 0.f;
    float peakOutputL_ = 0.f;
    float meterReleaseCoeff_ = 0.f;

    enum ParamIdx {
        P_THRESHOLD = 0, P_RATIO, P_ATTACK, P_RELEASE,
        P_MAKEUP, P_AUTO_MAKEUP, P_SIDECHAIN, P_SIDE_HPF, P_MIX,
        P_NUM_PARAMS
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrapeCompressor)
};

} // namespace y2k::dsp
