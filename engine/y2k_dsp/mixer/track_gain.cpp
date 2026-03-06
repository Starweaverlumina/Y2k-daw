// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/mixer/track_gain.cpp
// ═══════════════════════════════════════════════════════════════════

#include "track_gain.h"
#include <algorithm>
#include <cmath>

namespace y2k::dsp {

// Equal-power panning: maintains constant loudness across the stereo field.
// gainL = cos(angle), gainR = sin(angle)  where angle = (pan+1)*π/4
static void panToLR(float pan, float& gainL, float& gainR) noexcept {
    constexpr float kPiOver4 = 0.7853981633974483f;
    const float angle = (pan + 1.f) * kPiOver4;
    gainL = std::cos(angle);
    gainR = std::sin(angle);
}

TrackGainProcessor::TrackGainProcessor(float volume, float pan) noexcept
    : volume_(volume), pan_(pan)
{
    name      = "TrackGain";
    nodeColor = Y2KColor::Platinum();
}

void TrackGainProcessor::prepare(const ProcessSpec& spec) {
    spec_ = spec;
    smoothVolumeL_.reset(spec.sampleRate, 10.0);
    smoothVolumeR_.reset(spec.sampleRate, 10.0);

    // Initialise ParameterBank (v8: base=0, automation=0 initially)
    //   param 0 = volume  [0..1]
    //   param 1 = pan     [-1..+1] mapped as [0..1] in bank, scaled in process
    //   param 2 = mute    [0..1]
    initParameters(3);
    bank_.setRange(0, 0.f, 1.f);
    bank_.setRange(1, 0.f, 1.f);   // bank stores 0-1; process() maps to -1..+1
    bank_.setRange(2, 0.f, 1.f);
    bank_.setBase(0, volume_.load(std::memory_order_relaxed));
    bank_.setBase(1, (pan_.load(std::memory_order_relaxed) + 1.f) * 0.5f); // -1..1 -> 0..1
    bank_.setBase(2, muted_.load(std::memory_order_relaxed) ? 1.f : 0.f);

    // Snap to current values — no ramp on first prepare
    float gl, gr;
    panToLR(pan_.load(std::memory_order_relaxed), gl, gr);
    const float vol = volume_.load(std::memory_order_relaxed);
    smoothVolumeL_.snap(vol * gl);
    smoothVolumeR_.snap(vol * gr);

    outputBuf.setSize(2, spec.maxBlockSize, false, true, false);
    outputBuf.clear();
}

void TrackGainProcessor::reset() {
    outputBuf.clear();
}

// ─────────────────────────────────────────────────────────────────
//  process  [RT]
//
//  Reads inputBuf (filled by ProcessGraph from upstream connections),
//  applies smoothed volume+pan, writes to outputBuf.
//
//  RT contract:
//    ✓ No allocation
//    ✓ No locks (atomic loads with relaxed ordering)
//    ✓ SmoothParameter::advance() is non-allocating
// ─────────────────────────────────────────────────────────────────
void TrackGainProcessor::process(const ProcessContext& ctx) {
    const int N = ctx.blockSize;
    if (N <= 0) return;

    // If muted, write silence and return
    if (muted_.load(std::memory_order_relaxed)) {
        outputBuf.clear(0, N);
        return;
    }

    // Read unified resolved values — automation contributes here.
    // Automation writes to bank_ via setParameterAutomation(),
    // which adds to the base (not overwrites it).
    if (bank_.size() >= 2) {
        const float resolvedVol = bank_.resolved(0);               // 0..1
        const float resolvedPan = bank_.resolved(1) * 2.f - 1.f;  // 0..1 -> -1..+1
        // Only sync to atomics if they differ (avoid noise from tiny float deltas)
        if (std::abs(resolvedVol - volume_.load(std::memory_order_relaxed)) > 1e-5f)
            volume_.store(resolvedVol, std::memory_order_release);
        if (std::abs(resolvedPan - pan_.load(std::memory_order_relaxed)) > 1e-5f)
            pan_.store(std::clamp(resolvedPan, -1.f, 1.f), std::memory_order_release);
    }
    // Check if targets have changed and trigger ramps
    updateSmoothedTargets();

    const float* inL = inputBuf.getNumChannels() > 0 ? inputBuf.getReadPointer(0) : nullptr;
    const float* inR = inputBuf.getNumChannels() > 1 ? inputBuf.getReadPointer(1) : inL;
    float* outL = outputBuf.getWritePointer(0);
    float* outR = outputBuf.getWritePointer(1);

    if (!inL) {
        outputBuf.clear(0, N);
        return;
    }

    for (int s = 0; s < N; ++s) {
        const float gl = smoothVolumeL_.advance();
        const float gr = smoothVolumeR_.advance();
        outL[s] = (inL ? inL[s] : 0.f) * gl;
        outR[s] = (inR ? inR[s] : 0.f) * gr;
    }
}

void TrackGainProcessor::updateSmoothedTargets() noexcept {
    const float vol = volume_.load(std::memory_order_relaxed);
    const float pan = pan_   .load(std::memory_order_relaxed);
    float gl, gr;
    panToLR(pan, gl, gr);
    const float tl = vol * gl;
    const float tr = vol * gr;

    // Only trigger ramps when target has actually changed
    if (std::abs(tl - smoothVolumeL_.current()) > 1e-5f) {
        smoothVolumeL_.setTarget(tl);
        smoothVolumeL_.trigger();
    }
    if (std::abs(tr - smoothVolumeR_.current()) > 1e-5f) {
        smoothVolumeR_.setTarget(tr);
        smoothVolumeR_.trigger();
    }
}

// ─────────────────────────────────────────────────────────────────
//  Setters — all callable from UI thread
// ─────────────────────────────────────────────────────────────────
void TrackGainProcessor::setVolume(float linear) noexcept {
    const float v = std::clamp(linear, 0.f, 1.f);
    volume_.store(v, std::memory_order_release);
    if (bank_.size() > 0) bank_.setBase(0, v);
}

void TrackGainProcessor::setPan(float panPos) noexcept {
    const float p = std::clamp(panPos, -1.f, 1.f);
    pan_.store(p, std::memory_order_release);
    if (bank_.size() > 1) bank_.setBase(1, (p + 1.f) * 0.5f); // -1..+1 → 0..1
}

void TrackGainProcessor::setMuted(bool muted) noexcept {
    muted_.store(muted, std::memory_order_release);
    if (bank_.size() > 2) bank_.setBase(2, muted ? 1.f : 0.f);
}

// ─────────────────────────────────────────────────────────────────
//  Parameter accessors (0=volume, 1=pan, 2=mute)
// ─────────────────────────────────────────────────────────────────
float TrackGainProcessor::getParameter(int i) const {
    if (i == 0) return volume_.load(std::memory_order_acquire);
    if (i == 1) return pan_   .load(std::memory_order_acquire);
    if (i == 2) return muted_ .load(std::memory_order_acquire) ? 1.f : 0.f;
    return 0.f;
}

void TrackGainProcessor::setParameter(int i, float v) {
    if (i == 0) setVolume(v);
    if (i == 1) setPan(v);
    if (i == 2) setMuted(v >= 0.5f);
}

juce::String TrackGainProcessor::getParameterName(int i) const {
    if (i == 0) return "Volume";
    if (i == 1) return "Pan";
    if (i == 2) return "Mute";
    return {};
}

} // namespace y2k::dsp
