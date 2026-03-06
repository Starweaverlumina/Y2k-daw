#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/mixer/track_gain.h
//
//  TrackGainProcessor — per-track volume + pan in the DSP graph.
//
//  Lives between a source node and OUTPUT_NODE_ID:
//    BondiSynth → TrackGainProcessor → OUTPUT
//
//  Parameters:
//    0 = volume  (linear 0-1, default 0.8)
//    1 = pan     (-1 = full left, 0 = center, +1 = full right)
//    2 = mute    (0 = unmuted, 1 = muted)
//
//  All parameter changes are RT-safe via atomic setters.
//  Gain smoothing uses SmoothParameter (10 ms ramp) to prevent clicks.
// ═══════════════════════════════════════════════════════════════════

#include "../graph/processor_graph.h"
#include <atomic>
#include <cmath>

namespace y2k::dsp {

class TrackGainProcessor final : public ProcessorNode {
public:
    explicit TrackGainProcessor(float volume = 0.8f, float pan = 0.f) noexcept;

    // Thread-safe setters (UI thread → audio thread)
    void setVolume(float linear)  noexcept;
    void setPan   (float panPos)  noexcept;  // -1..+1
    void setMuted (bool muted)    noexcept;

    float getVolume() const noexcept { return volume_.load(std::memory_order_acquire); }
    float getPan()    const noexcept { return pan_   .load(std::memory_order_acquire); }
    bool  isMuted()   const noexcept { return muted_ .load(std::memory_order_acquire); }

    // ProcessorNode interface
    void prepare(const ProcessSpec& spec) override;
    void process(const ProcessContext& ctx) override;
    void reset()                           override;

    int getNumInputChannels()  const override { return 2; }
    int getNumOutputChannels() const override { return 2; }

    int          getNumParameters()       const override { return 3; }
    float        getParameter(int i)      const override;
    void         setParameter(int i, float v) override;
    juce::String getParameterName(int i)  const override;

private:
    std::atomic<float> volume_ {0.8f};
    std::atomic<float> pan_    {0.f};
    std::atomic<bool>  muted_  {false};

    SmoothParameter smoothVolumeL_;
    SmoothParameter smoothVolumeR_;

    void updateSmoothedTargets() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackGainProcessor)
};

} // namespace y2k::dsp
