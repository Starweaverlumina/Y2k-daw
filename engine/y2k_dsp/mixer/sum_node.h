#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/mixer/sum_node.h  — v9
//
//  SumNode — explicit mix/convergence point in the DSP graph.
//
//  This node exists purely to give the PDC engine a stable, named
//  location where multiple signal paths converge. Without it, the
//  PDC algorithm has to infer convergence from edge topology, which
//  breaks when new nodes are inserted between tracks and the output.
//
//  Role in graph topology:
//    Track A chain  ─┐
//    Track B chain  ─┤─→ SumNode → BusFX → BusFader → MasterSum
//    Send from X   ─┘
//
//  Audio behavior:
//    Sums all channels from inputBuf into outputBuf.
//    inputBuf is filled by ProcessGraph from all incoming connections.
//    output = sum of all inputs (no gain scaling — that's TrackFaderNode)
//
//  PDC role:
//    isSumNode() returns true → PDC engine computes max incoming
//    cumulative latency, inserts LatencyCompensatorNode on shorter paths.
//
//  Thread safety:
//    RT-safe: no allocation, no lock.
//    setGain() from UI: atomic store.
//
//  Parameters:
//    0 = outputGain (linear, 1.0 = unity, default 1.0)
// ═══════════════════════════════════════════════════════════════════

#include "../graph/processor_graph.h"
#include <atomic>
#include <algorithm>
#include <cmath>

namespace y2k::dsp {

class SumNode final : public ProcessorNode {
public:
    // numIn  — declared input channels (sets input buffer sizing)
    // numOut — declared output channels
    explicit SumNode(int numIn=2, int numOut=2, float gain=1.f) noexcept
        : numIn_(numIn), numOut_(numOut), gain_(gain)
    {
        name      = "SumNode";
        nodeColor = Y2KColor::Platinum();
    }

    // ── Identity ──────────────────────────────────────────────────
    bool isSumNode() const noexcept override { return true; }

    int getNumInputChannels()  const override { return numIn_;  }
    int getNumOutputChannels() const override { return numOut_; }
    int getLatencySamples()    const noexcept override { return 0; }
    int getNumParameters()     const override { return 1; }
    juce::String getParameterName(int i) const override {
        return (i==0) ? "Output Gain" : juce::String{};
    }

    void setGain(float g) noexcept {
        gain_.store(std::clamp(g, 0.f, 4.f), std::memory_order_release);
        if (bank_.size() > 0) bank_.setBase(0, gain_.load(std::memory_order_relaxed));
    }
    float getGain() const noexcept { return gain_.load(std::memory_order_acquire); }

    // ── Lifecycle ─────────────────────────────────────────────────
    void prepare(const ProcessSpec& spec) override {
        spec_ = spec;
        initParameters(1);
        bank_.setRange(0, 0.f, 4.f);
        bank_.setBase(0, gain_.load(std::memory_order_relaxed));
        outputBuf.setSize(numOut_, std::max(1, spec.maxBlockSize), false, true, false);
        outputBuf.clear();
        inputBuf .setSize(numIn_,  std::max(1, spec.maxBlockSize), false, true, false);
        inputBuf .clear();
    }

    void reset() override { outputBuf.clear(); inputBuf.clear(); }

    // ── process [RT — no allocation, no lock] ────────────────────
    //
    //  ProcessGraph fills inputBuf from all upstream connections.
    //  SumNode copies inputBuf → outputBuf applying output gain.
    //  The "summing" is done implicitly: ProcessGraph uses
    //  FloatVectorOperations::add to accumulate multiple sources
    //  into the inputBuf before this node's process() is called.
    //  So inputBuf already contains the mix — we just scale + copy.
    void process(const ProcessContext& ctx) override {
        const int N    = ctx.blockSize;
        const float g  = bank_.size() > 0 ? bank_.resolved(0)
                                           : gain_.load(std::memory_order_relaxed);

        for (int ch = 0; ch < numOut_ && ch < numIn_; ++ch) {
            const float* in  = inputBuf .getReadPointer(std::min(ch, inputBuf.getNumChannels()-1));
            float*       out = outputBuf.getWritePointer(ch);
            if (std::abs(g - 1.f) < 1e-5f) {
                juce::FloatVectorOperations::copy(out, in, N);
            } else {
                juce::FloatVectorOperations::copyWithMultiply(out, in, g, N);
            }
        }
    }

private:
    int               numIn_, numOut_;
    std::atomic<float> gain_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SumNode)
};

} // namespace y2k::dsp
