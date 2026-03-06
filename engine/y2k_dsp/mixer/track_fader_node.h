#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/mixer/track_fader_node.h  — v9
//
//  TrackFaderNode — per-track volume/pan/mute/solo DSP node.
//
//  Supersedes TrackGainProcessor (v8) with:
//    • Explicit send tap point management (pre/post)
//    • Solo/mute interlock via SoloManager (from AudioEngine)
//    • Full ParameterBank integration from construction
//    • getLatencySamples() = 0 (fader is latency-free)
//    • Metering data written via MeterTap (separate node downstream)
//
//  Parameters (ParameterBank):
//    0 = volume     (0..1 linear, default 0.8)
//    1 = pan        (0..1 → mapped to -1..+1 in DSP)
//    2 = mute       (0=unmuted, 1=muted)
//    3 = solo       (0=normal, 1=soloed)
//    4 = preFadeVol (0..1 pre-fader volume for sends, default 1.0)
//
//  Thread model:
//    setVolume/setPan/setMuted/setSoloed — UI thread (atomic stores)
//    process() — audio thread (reads atomics via bank_.resolved())
// ═══════════════════════════════════════════════════════════════════

#include "../graph/processor_graph.h"
#include <atomic>
#include <cmath>

namespace y2k::dsp {

// Forward declared — AudioEngine provides a pointer to this
class SoloManager {
public:
    // Returns true if ANY track is soloed and this track is NOT soloed
    bool isMutedBySolo(uint32_t nodeId) const noexcept {
        if (!anySoloed_.load(std::memory_order_relaxed)) return false;
        return !soloed_.load(std::memory_order_relaxed);
    }
    void setSoloed(bool s)   noexcept { soloed_.store(s,    std::memory_order_release); }
    void setAnySoloed(bool s) noexcept { anySoloed_.store(s, std::memory_order_release); }
private:
    std::atomic<bool> soloed_    {false};
    std::atomic<bool> anySoloed_ {false};
};

class TrackFaderNode final : public ProcessorNode {
public:
    enum Param : int { VOLUME=0, PAN=1, MUTE=2, SOLO=3, PREFADE_VOL=4 };

    explicit TrackFaderNode(float volume=0.8f, float pan=0.f) noexcept
        : volume_(volume), pan_(pan)
    {
        name = "TrackFader";
        nodeColor = Y2KColor::Tangerine();
    }

    // ── UI-thread setters ─────────────────────────────────────────
    void setVolume(float v)     noexcept;
    void setPan   (float p)     noexcept;
    void setMuted (bool m)      noexcept;
    void setSoloed(bool s)      noexcept;
    void setSoloManager(SoloManager* sm) noexcept { soloManager_ = sm; }

    float getVolume() const noexcept { return volume_.load(std::memory_order_acquire); }
    float getPan()    const noexcept { return pan_   .load(std::memory_order_acquire); }
    bool  isMuted()   const noexcept { return muted_ .load(std::memory_order_acquire); }
    bool  isSoloed()  const noexcept { return soloed_.load(std::memory_order_acquire); }

    // ── ProcessorNode ─────────────────────────────────────────────
    void prepare(const ProcessSpec&) override;
    void process(const ProcessContext&) override;
    void reset()  override { outputBuf.clear(); }

    int getNumInputChannels()  const override { return 2; }
    int getNumOutputChannels() const override { return 2; }
    int getLatencySamples()    const noexcept override { return 0; }
    int getNumParameters()     const override { return 5; }

    float        getParameter(int i)   const override;
    void         setParameter(int i, float v) override;
    juce::String getParameterName(int i) const override;

private:
    static void panToLR(float pan, float& gl, float& gr) noexcept {
        constexpr float kPiOver4 = 0.7853981633974483f;
        const float angle = (pan + 1.f) * kPiOver4;
        gl = std::cos(angle); gr = std::sin(angle);
    }

    std::atomic<float> volume_ {0.8f};
    std::atomic<float> pan_    {0.f};
    std::atomic<bool>  muted_  {false};
    std::atomic<bool>  soloed_ {false};

    SmoothParameter smoothL_, smoothR_;
    SoloManager*    soloManager_ = nullptr;

    void updateSmoothed() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackFaderNode)
};

// ─────────────────────────────────────────────────────────────────
//  inline implementations (header-only for simple setters)
// ─────────────────────────────────────────────────────────────────
inline void TrackFaderNode::setVolume(float v) noexcept {
    const float c = std::clamp(v, 0.f, 1.f);
    volume_.store(c, std::memory_order_release);
    if (bank_.size() > VOLUME) bank_.setBase(VOLUME, c);
}
inline void TrackFaderNode::setPan(float p) noexcept {
    const float c = std::clamp(p, -1.f, 1.f);
    pan_.store(c, std::memory_order_release);
    if (bank_.size() > PAN) bank_.setBase(PAN, (c+1.f)*0.5f);
}
inline void TrackFaderNode::setMuted(bool m) noexcept {
    muted_.store(m, std::memory_order_release);
    if (bank_.size() > MUTE) bank_.setBase(MUTE, m ? 1.f : 0.f);
}
inline void TrackFaderNode::setSoloed(bool s) noexcept {
    soloed_.store(s, std::memory_order_release);
    if (bank_.size() > SOLO) bank_.setBase(SOLO, s ? 1.f : 0.f);
    if (soloManager_) soloManager_->setSoloed(s);
}

inline void TrackFaderNode::prepare(const ProcessSpec& spec) {
    spec_ = spec;
    initParameters(5);
    bank_.setRange(VOLUME,    0.f, 1.f);
    bank_.setRange(PAN,       0.f, 1.f);
    bank_.setRange(MUTE,      0.f, 1.f);
    bank_.setRange(SOLO,      0.f, 1.f);
    bank_.setRange(PREFADE_VOL, 0.f, 1.f);
    bank_.setBase(VOLUME, volume_.load(std::memory_order_relaxed));
    bank_.setBase(PAN,    (pan_.load(std::memory_order_relaxed)+1.f)*0.5f);
    bank_.setBase(MUTE,   muted_.load(std::memory_order_relaxed) ? 1.f : 0.f);
    bank_.setBase(SOLO,   soloed_.load(std::memory_order_relaxed) ? 1.f : 0.f);
    bank_.setBase(PREFADE_VOL, 1.f);

    smoothL_.reset(spec.sampleRate, 10.0);
    smoothR_.reset(spec.sampleRate, 10.0);
    float gl, gr;
    panToLR(pan_.load(std::memory_order_relaxed), gl, gr);
    const float vol = volume_.load(std::memory_order_relaxed);
    smoothL_.snap(vol * gl);
    smoothR_.snap(vol * gr);

    outputBuf.setSize(2, std::max(1, spec.maxBlockSize), false, true, false);
    outputBuf.clear();
}

inline void TrackFaderNode::updateSmoothed() noexcept {
    const float vol = bank_.size() > VOLUME ? bank_.resolved(VOLUME)
                                            : volume_.load(std::memory_order_relaxed);
    const float pan = bank_.size() > PAN
                    ? bank_.resolved(PAN) * 2.f - 1.f
                    : pan_.load(std::memory_order_relaxed);
    float gl, gr;
    panToLR(pan, gl, gr);
    const float tl = vol * gl, tr = vol * gr;
    if (std::abs(tl - smoothL_.current()) > 1e-5f) { smoothL_.setTarget(tl); smoothL_.trigger(); }
    if (std::abs(tr - smoothR_.current()) > 1e-5f) { smoothR_.setTarget(tr); smoothR_.trigger(); }
}

inline void TrackFaderNode::process(const ProcessContext& ctx) {
    const int N = ctx.blockSize;
    if (N <= 0) return;

    const bool muted = muted_.load(std::memory_order_relaxed)
                    || (soloManager_ && soloManager_->isMutedBySolo(nodeId));
    if (muted) { outputBuf.clear(0, N); return; }

    updateSmoothed();

    const float* inL = inputBuf.getNumChannels() > 0 ? inputBuf.getReadPointer(0) : nullptr;
    const float* inR = inputBuf.getNumChannels() > 1 ? inputBuf.getReadPointer(1) : inL;
    float* outL = outputBuf.getWritePointer(0);
    float* outR = outputBuf.getWritePointer(1);

    if (!inL) { outputBuf.clear(0, N); return; }

    for (int s = 0; s < N; ++s) {
        outL[s] = inL[s] * smoothL_.advance();
        outR[s] = (inR ? inR[s] : 0.f) * smoothR_.advance();
    }
}

inline float TrackFaderNode::getParameter(int i) const {
    switch (i) {
        case VOLUME: return volume_.load(std::memory_order_acquire);
        case PAN:    return pan_   .load(std::memory_order_acquire);
        case MUTE:   return muted_ .load(std::memory_order_acquire) ? 1.f : 0.f;
        case SOLO:   return soloed_.load(std::memory_order_acquire) ? 1.f : 0.f;
        case PREFADE_VOL: return bank_.size() > PREFADE_VOL ? bank_.getBase(PREFADE_VOL) : 1.f;
        default: return 0.f;
    }
}
inline void TrackFaderNode::setParameter(int i, float v) {
    switch (i) {
        case VOLUME: setVolume(v); break;
        case PAN:    setPan(v);    break;
        case MUTE:   setMuted(v >= 0.5f); break;
        case SOLO:   setSoloed(v >= 0.5f); break;
        case PREFADE_VOL: if (bank_.size() > PREFADE_VOL) bank_.setBase(PREFADE_VOL, v); break;
        default: break;
    }
}
inline juce::String TrackFaderNode::getParameterName(int i) const {
    static const char* names[] = {"Volume","Pan","Mute","Solo","Pre-Fader Vol"};
    return (i>=0&&i<5) ? names[i] : juce::String{};
}

} // namespace y2k::dsp
