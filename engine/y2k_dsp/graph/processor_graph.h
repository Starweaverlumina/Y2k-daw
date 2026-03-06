#pragma once
// PDC engine included in .cpp — forward declare here
// to keep header lean. PDCEngine::computeAndInject called in commitState().
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/graph/processor_graph.h  — v8
// ═══════════════════════════════════════════════════════════════════

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include "../param/parameter_state.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace y2k::dsp {

struct GraphState;

struct ProcessSpec {
    double sampleRate   = 44100.0;
    int    maxBlockSize = 512;
    int    numChannels  = 2;
};

struct ProcessContext {
    juce::AudioBuffer<float>* inputBuffer  = nullptr;
    juce::AudioBuffer<float>* outputBuffer = nullptr;
    juce::MidiBuffer*         midiBuffer   = nullptr;
    double                    sampleRate   = 44100.0;
    int                       blockSize    = 0;
    double                    transportBPM = 120.0;
    bool                      isPlaying    = false;
    double                    ppqPosition  = 0.0;
};

// ─────────────────────────────────────────────────────────────────
//  ParameterEvent  layer: 0=base 1=auto 2=mod 3=midi
// ─────────────────────────────────────────────────────────────────
struct ParameterEvent {
    uint32_t nodeId  = 0;
    uint16_t paramId = 0;
    uint8_t  layer   = 0;
    float    value   = 0.f;
};

// ─────────────────────────────────────────────────────────────────
//  MidiEvent — compact, no alloc
// ─────────────────────────────────────────────────────────────────
struct MidiEvent {
    uint8_t  data[3]     = {0,0,0};
    uint8_t  size        = 0;
    int      sampleOffset = 0;

    static MidiEvent noteOn(int ch, int note, int vel, int off=0) noexcept {
        MidiEvent e; e.data[0]=uint8_t(0x90|(ch-1)); e.data[1]=uint8_t(note);
        e.data[2]=uint8_t(vel); e.size=3; e.sampleOffset=off; return e;
    }
    static MidiEvent noteOff(int ch, int note, int off=0) noexcept {
        MidiEvent e; e.data[0]=uint8_t(0x80|(ch-1)); e.data[1]=uint8_t(note);
        e.data[2]=0; e.size=3; e.sampleOffset=off; return e;
    }
    static MidiEvent cc(int ch, int cc, int val, int off=0) noexcept {
        MidiEvent e; e.data[0]=uint8_t(0xB0|(ch-1)); e.data[1]=uint8_t(cc);
        e.data[2]=uint8_t(val); e.size=3; e.sampleOffset=off; return e;
    }
    juce::MidiMessage toJuce() const noexcept {
        if (!size) return juce::MidiMessage();
        return juce::MidiMessage(data[0], size>1?data[1]:0, size>2?data[2]:0);
    }
};

// ─────────────────────────────────────────────────────────────────
//  LockFreeQueue — SPSC wait-free ring buffer
// ─────────────────────────────────────────────────────────────────
template<typename T, size_t Capacity = 1024>
class LockFreeQueue {
public:
    bool push(const T& item) noexcept {
        const auto h = head_.load(std::memory_order_relaxed);
        const auto nh = advance(h);
        if (nh == tail_.load(std::memory_order_acquire)) return false;
        buffer_[h] = item;
        head_.store(nh, std::memory_order_release);
        return true;
    }
    std::optional<T> pop() noexcept {
        const auto t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return std::nullopt;
        T item = buffer_[t];
        tail_.store(advance(t), std::memory_order_release);
        return item;
    }
    bool isEmpty() const noexcept {
        return head_.load(std::memory_order_acquire)==tail_.load(std::memory_order_acquire);
    }
    void clear() noexcept { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }
private:
    static constexpr size_t N = Capacity + 1;
    static constexpr size_t advance(size_t i) noexcept { return (i+1)%N; }
    std::array<T,N> buffer_{};
    std::atomic<size_t> head_{0}, tail_{0};
};

// ─────────────────────────────────────────────────────────────────
//  SmoothParameter — 10ms ramp, no atomic on hot path
// ─────────────────────────────────────────────────────────────────
class SmoothParameter {
public:
    void reset(double sr, double ms=10.0) {
        rampSamples_=int(sr*ms/1000.0);
        currentValue_=targetValue_.load(std::memory_order_relaxed);
        samplesRemaining_=0;
    }
    void setTarget(float t) noexcept { targetValue_.store(t,std::memory_order_release); }
    void trigger()  noexcept { samplesRemaining_=rampSamples_; }
    void snap(float v) noexcept { targetValue_.store(v,std::memory_order_release);currentValue_=v;samplesRemaining_=0; }
    float advance() noexcept {
        const float tgt=targetValue_.load(std::memory_order_acquire);
        if(samplesRemaining_<=0){currentValue_=tgt;return currentValue_;}
        currentValue_+=(tgt-currentValue_)/float(samplesRemaining_--);
        return currentValue_;
    }
    float current() const noexcept { return currentValue_; }
private:
    std::atomic<float> targetValue_{0.f};
    float currentValue_{0.f};
    int samplesRemaining_{0}, rampSamples_{441};
};

struct Point2D { float x=0.f,y=0.f; };
struct Y2KColor {
    uint8_t r,g,b,a=255;
    static constexpr Y2KColor BondiBlue()  {return{0x00,0x71,0xe3};}
    static constexpr Y2KColor Tangerine()  {return{0xff,0x95,0x00};}
    static constexpr Y2KColor Grape()      {return{0xaf,0x52,0xde};}
    static constexpr Y2KColor Lime()       {return{0xa6,0xe2,0x2e};}
    static constexpr Y2KColor Strawberry() {return{0xff,0x2d,0x55};}
    static constexpr Y2KColor Platinum()   {return{0xe8,0xe8,0xe8};}
};

struct Connection {
    uint32_t sourceNodeId;
    int      sourceChannelIndex;
    uint32_t destNodeId;
    int      destChannelIndex;
};

// ─────────────────────────────────────────────────────────────────
//  ProcessorNode — v8 (with ParameterBank)
// ─────────────────────────────────────────────────────────────────
class ProcessorNode {
public:
    virtual ~ProcessorNode() = default;
    virtual void prepare(const ProcessSpec& spec) = 0;
    virtual void process(const ProcessContext& ctx) = 0;
    virtual void reset() {}

    uint32_t     nodeId    = 0;
    juce::String name;
    Y2KColor     nodeColor = Y2KColor::BondiBlue();
    Point2D      visualPos;

    virtual int          getNumInputChannels()  const { return 2; }
    virtual int          getNumOutputChannels() const { return 2; }
    virtual int          getNumParameters()     const { return bank_.size(); }

    // ── Plugin Delay Compensation ─────────────────────────────────
    // Processors that introduce latency (lookahead compressors,
    // linear-phase EQ, pitch correction, FIR filters) must override
    // this and return their latency in samples.
    // Latency changes require a non-RT commitState() call, never live mutation.
    virtual int          getLatencySamples()    const noexcept { return 0; }

    // Called by PDC engine during commitState() to mark if this node is a
    // mix/sum point (multiple inputs converging). Subclass or let graph detect.
    virtual bool         isSumNode()            const noexcept { return false; }

    // Delegates to ParameterBank base layer (UI thread)
    virtual float        getParameter(int i)        const { return bank_.getBase(i); }
    virtual void         setParameter(int i, float v)     { bank_.setBase(i, v); }
    virtual juce::String getParameterName(int i)    const { (void)i; return {}; }

    // Layered writes (audio thread only — no lock needed)
    void setParameterAutomation(int i, float v) noexcept { bank_.setAutomation(i, v); }
    void setParameterMidi      (int i, float v) noexcept { bank_.setMidi(i, v); }

    // Read unified resolved value [RT]
    float resolvedParameter(int i) const noexcept { return bank_.resolved(i); }

    juce::AudioBuffer<float> inputBuf;
    juce::AudioBuffer<float> outputBuf;
    bool isBypassed = false;

protected:
    ProcessSpec   spec_;
    ParameterBank bank_;

    void initParameters(int n) { bank_.initialise(n); }
};

// ─────────────────────────────────────────────────────────────────
//  ProcessGraph — v8 (atomic GraphState swap)
// ─────────────────────────────────────────────────────────────────
class ProcessGraph {
public:
    using NodePtr = std::unique_ptr<ProcessorNode>;

    ProcessGraph();
    ~ProcessGraph();

    // Non-RT graph editing
    ProcessorNode* addNode(NodePtr node);
    bool           removeNode(uint32_t nodeId);
    bool           connect(const Connection& c);
    bool           disconnect(const Connection& c);
    void           clear();

    // Non-RT: rebuild topo + commit new snapshot atomically.
    void prepare(const ProcessSpec& spec);  // also commits
    void commitState();                      // rebuild + swap only

    // Queries (non-RT)
    ProcessorNode*                 getNode(uint32_t nodeId);
    const ProcessorNode*           getNode(uint32_t nodeId) const;
    std::vector<uint32_t>          getNodeIds() const;
    const std::vector<Connection>& getConnections() const;

    // RT interface
    std::shared_ptr<const GraphState> acquireState() const noexcept;

    // Graph versioning (increments on every commitState)
    uint64_t graphRevision() const noexcept { return graphRevision_.load(std::memory_order_acquire); }

    // PDC debug: returns latency report string (non-RT, call from UI)
    juce::String pdcDebugDump() const;

    // Total compensators injected in last commitState()
    int lastPdcCompensatorCount() const noexcept {
        return lastPdcCount_.load(std::memory_order_acquire);
    }
    void processBlock(juce::AudioBuffer<float>& audio,
                      juce::MidiBuffer&         midi,
                      const ProcessContext&     ctx);

    // Any thread → SPSC queue
    void pushParameterEvent(const ParameterEvent& evt);

    static constexpr uint32_t OUTPUT_NODE_ID = 0xFFFFFFFF;
    static constexpr uint32_t INPUT_NODE_ID  = 0xFFFFFFFE;

private:
    void rebuildExecutionOrder(std::vector<ProcessorNode*>& out) const;
    void drainParameterQueue();

    std::vector<NodePtr>    nodes_;
    std::vector<Connection> connections_;

    // C++20 atomic<shared_ptr<>> — lock-free on x86-64/arm64
    std::atomic<std::shared_ptr<const GraphState>> activeState_;

    ProcessSpec currentSpec_;
    LockFreeQueue<ParameterEvent, 2048> paramQueue_;
    std::atomic<uint64_t> graphRevision_   {0};
    std::atomic<int>      lastPdcCount_    {0};
    uint32_t    nextNodeId_ = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessGraph)
};

} // namespace y2k::dsp
