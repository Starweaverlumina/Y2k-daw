#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/mixer/send_tap_node.h  — v9
//
//  SendTapNode — routes a copy of its input to a bus sum node.
//
//  Placement in graph:
//    Pre-fader send:
//      InsertChain → [SendTapNode] → TrackFader → ...
//                         ↓ (separate edge)
//                      BusSumNode
//
//    Post-fader send:
//      InsertChain → TrackFader → [SendTapNode] → MasterSum
//                                      ↓ (separate edge)
//                                   BusSumNode
//
//  Audio behavior:
//    • Copy input → output (passthrough, no modification to main path)
//    • Apply send level to the copy
//    • The copy is wired to the bus sum via a separate Connection
//      in the compiled ProcessGraph
//
//  PDC implications:
//    • SendTapNode introduces 0 samples of latency
//    • The cumulative latency along the send path may differ from
//      the direct path → PDC injects a compensator at the BusSumNode
//
//  Parameters:
//    0 = sendLevel (0..1, default 1.0)
//    1 = enabled   (0=disabled, 1=enabled)
//
//  Thread model:
//    UI: setSendLevel / setEnabled via atomics + bank_ base layer
//    RT: process() reads bank_.resolved()
// ═══════════════════════════════════════════════════════════════════

#include "../graph/processor_graph.h"
#include <atomic>
#include <cmath>

namespace y2k::dsp {

class SendTapNode final : public ProcessorNode {
public:
    enum Param : int { SEND_LEVEL=0, ENABLED=1 };

    // targetBusNodeId: the SumNode this send feeds into.
    // GraphCompiler creates a Connection from this node's output → that SumNode.
    explicit SendTapNode(uint32_t targetBusNodeId=0, float sendLevel=1.f) noexcept
        : targetBusNodeId_(targetBusNodeId), sendLevel_(sendLevel)
    {
        name = "SendTap";
        nodeColor = Y2KColor::Grape();
    }

    uint32_t targetBusNodeId() const noexcept { return targetBusNodeId_; }

    void setSendLevel(float l) noexcept {
        const float c = std::clamp(l, 0.f, 1.f);
        sendLevel_.store(c, std::memory_order_release);
        if (bank_.size() > SEND_LEVEL) bank_.setBase(SEND_LEVEL, c);
    }
    void setEnabled(bool e) noexcept {
        enabled_.store(e, std::memory_order_release);
        if (bank_.size() > ENABLED) bank_.setBase(ENABLED, e ? 1.f : 0.f);
    }
    float getSendLevel() const noexcept { return sendLevel_.load(std::memory_order_acquire); }
    bool  isEnabled()    const noexcept { return enabled_ .load(std::memory_order_acquire); }

    // ── ProcessorNode ─────────────────────────────────────────────
    void prepare(const ProcessSpec& spec) override {
        spec_ = spec;
        initParameters(2);
        bank_.setRange(SEND_LEVEL, 0.f, 1.f);
        bank_.setRange(ENABLED,    0.f, 1.f);
        bank_.setBase(SEND_LEVEL, sendLevel_.load(std::memory_order_relaxed));
        bank_.setBase(ENABLED, enabled_.load(std::memory_order_relaxed) ? 1.f : 0.f);

        // Size for 2-channel pass-through
        outputBuf.setSize(2, std::max(1, spec.maxBlockSize), false, true, false);
        outputBuf.clear();
        inputBuf .setSize(2, std::max(1, spec.maxBlockSize), false, true, false);
        inputBuf .clear();
        sendBuf  .setSize(2, std::max(1, spec.maxBlockSize), false, true, false);
        sendBuf  .clear();
    }

    void reset() override { outputBuf.clear(); sendBuf.clear(); }

    // ── process [RT] ──────────────────────────────────────────────
    //  Main path: input → output (passthrough, unity gain)
    //  Send path: input * sendLevel → sendBuf
    //             GraphCompiler adds a Connection from this node
    //             to the target BusSumNode, using sendBuf.
    //  Note: ProcessGraph::processBlock routes by Connection only.
    //        We provide two output channels:
    //          ch 0,1 = main passthrough
    //          ch 2,3 = send mix (sendLevel applied)
    //        GraphCompiler sets connections accordingly.
    //
    //  In practice we use a single output buffer with 4 channels.
    //  Main: ch 0+1 (passthrough)
    //  Send: ch 2+3 (level-scaled copy)
    void process(const ProcessContext& ctx) override {
        const int N = ctx.blockSize;
        if (N <= 0) return;

        const bool   en  = enabled_.load(std::memory_order_relaxed);
        const float  lvl = bank_.size() > SEND_LEVEL
                         ? bank_.resolved(SEND_LEVEL)
                         : sendLevel_.load(std::memory_order_relaxed);

        // Main passthrough (ch 0+1)
        for (int ch = 0; ch < 2; ++ch) {
            const float* in  = ch < inputBuf.getNumChannels()
                             ? inputBuf.getReadPointer(ch) : nullptr;
            float*       out = outputBuf.getWritePointer(ch);
            if (in) juce::FloatVectorOperations::copy(out, in, N);
            else    juce::FloatVectorOperations::clear(out, N);
        }

        // Send copy (ch 2+3) — level-scaled
        if (outputBuf.getNumChannels() >= 4) {
            for (int ch = 0; ch < 2; ++ch) {
                const float* in  = ch < inputBuf.getNumChannels()
                                 ? inputBuf.getReadPointer(ch) : nullptr;
                float*       out = outputBuf.getWritePointer(ch + 2);
                if (en && in)
                    juce::FloatVectorOperations::copyWithMultiply(out, in, lvl, N);
                else
                    juce::FloatVectorOperations::clear(out, N);
            }
        }
    }

    int getNumInputChannels()  const override { return 2; }
    int getNumOutputChannels() const override { return 4; } // 0+1=main, 2+3=send
    int getLatencySamples()    const noexcept override { return 0; }
    int getNumParameters()     const override { return 2; }

    float        getParameter(int i)   const override {
        if (i==SEND_LEVEL) return sendLevel_.load(std::memory_order_acquire);
        if (i==ENABLED)    return enabled_  .load(std::memory_order_acquire) ? 1.f : 0.f;
        return 0.f;
    }
    void         setParameter(int i, float v) override {
        if (i==SEND_LEVEL) setSendLevel(v);
        if (i==ENABLED)    setEnabled(v >= 0.5f);
    }
    juce::String getParameterName(int i) const override {
        if (i==SEND_LEVEL) return "Send Level";
        if (i==ENABLED)    return "Enabled";
        return {};
    }

    // Public for GraphCompiler (allocation happens non-RT)
    juce::AudioBuffer<float> sendBuf;

private:
    uint32_t           targetBusNodeId_;
    std::atomic<float> sendLevel_;
    std::atomic<bool>  enabled_ {true};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SendTapNode)
};

} // namespace y2k::dsp
