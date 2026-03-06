// ═══════════════════════════════════════════════════════════════════
//  Y2K DSP — "Grape Compressor" Implementation
//  Opto-style smooth compressor with VU meter output.
//  RT-safe: no allocation / mutex inside process().
// ═══════════════════════════════════════════════════════════════════

#include "grape_compressor.h"
#include <cmath>
#include <algorithm>

namespace y2k::dsp {

// ─────────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────────
GrapeCompressor::GrapeCompressor()
{
    name      = "Grape Compressor";
    nodeColor = Y2KColor::Grape();

    // Zero out all state explicitly
    hpfStateL_[0] = hpfStateL_[1] = 0.f;
    hpfStateR_[0] = hpfStateR_[1] = 0.f;
    envL_          = 0.f;
    envR_          = 0.f;
    currentGainDb_ = 0.f;
    peakInputL_    = 0.f;
    peakOutputL_   = 0.f;
    attackCoeff_   = 0.f;
    releaseCoeff_  = 0.f;
    meterReleaseCoeff_ = 0.f;
}

// ─────────────────────────────────────────────────────────────────
//  prepare
// ─────────────────────────────────────────────────────────────────
void GrapeCompressor::prepare(const ProcessSpec& spec)
{
    spec_ = spec;

    // Pre-allocate I/O buffers — never resize inside process()
    inputBuf.setSize (2, std::max(1, spec.maxBlockSize), false, true, false);
    outputBuf.setSize(2, std::max(1, spec.maxBlockSize), false, true, false);
    inputBuf.clear();
    outputBuf.clear();

    // Initialise ParameterBank for all parameters
    initParameters(P_NUM_PARAMS);
    for (int i = 0; i < P_NUM_PARAMS; ++i)
        bank_.setRange(i, 0.f, 100.f);

    // Seed bank with current params_ defaults
    bank_.setBase(P_THRESHOLD,  params_.threshold);
    bank_.setBase(P_RATIO,      params_.ratio);
    bank_.setBase(P_ATTACK,     params_.attack);
    bank_.setBase(P_RELEASE,    params_.release);
    bank_.setBase(P_MAKEUP,     params_.makeupGain);
    bank_.setBase(P_AUTO_MAKEUP, params_.autoMakeup ? 100.f : 0.f);
    bank_.setBase(P_SIDECHAIN,  params_.sidechain  ? 100.f : 0.f);
    bank_.setBase(P_SIDE_HPF,   params_.sideHPF);
    bank_.setBase(P_MIX,        params_.mix);

    // Smooth makeup: 20ms ramp for click-free makeup gain changes
    smoothMakeup_.reset(spec.sampleRate, 20.0);
    smoothMakeup_.snap(0.f);

    // Compute all filter / envelope coefficients
    applyParameters();

    // Reset runtime state
    reset();
}

// ─────────────────────────────────────────────────────────────────
//  reset
// ─────────────────────────────────────────────────────────────────
void GrapeCompressor::reset()
{
    envL_          = 0.f;
    envR_          = 0.f;
    currentGainDb_ = 0.f;
    peakInputL_    = 0.f;
    peakOutputL_   = 0.f;
    hpfStateL_[0] = hpfStateL_[1] = 0.f;
    hpfStateR_[0] = hpfStateR_[1] = 0.f;

    gainReductionDb_.store(0.f,   std::memory_order_relaxed);
    inputLevelDb_.store  (-96.f,  std::memory_order_relaxed);
    outputLevelDb_.store (-96.f,  std::memory_order_relaxed);
}

// ─────────────────────────────────────────────────────────────────
//  process  — full stereo opto compressor  [RT-safe]
//
//  All per-block work here: no new/delete/malloc, no mutex.
// ─────────────────────────────────────────────────────────────────
void GrapeCompressor::process(const ProcessContext& ctx)
{
    if (!ctx.inputBuffer || !ctx.outputBuffer) return;

    const int numSamples = ctx.blockSize;
    if (numSamples <= 0) return;

    const juce::AudioBuffer<float>& inBuf  = *ctx.inputBuffer;
    juce::AudioBuffer<float>&       outBuf = *ctx.outputBuffer;

    const int inCh  = inBuf.getNumChannels();
    const int outCh = outBuf.getNumChannels();
    if (inCh < 1 || outCh < 1) return;

    // ── Re-read parameter mappings ────────────────────────────────
    // (Parameters may have changed via setParameter() on UI thread;
    //  we only call applyParameters() when setParameter() is called,
    //  so coefficients are always current here. We just re-read
    //  the threshold/ratio/etc. for this block's computations.)
    const float threshDb  = thresholdDb();
    const float ratioVal  = ratio();
    const float mixFrac   = std::clamp(params_.mix / 100.f, 0.f, 1.f);

    // ── Compute auto makeup gain (dB) ─────────────────────────────
    float mkdB = 0.f;
    if (params_.autoMakeup && params_.makeupGain == 0.f) {
        // Classic formula: roughly compensates gain reduction at threshold
        mkdB = -threshDb * (1.f - 1.f / ratioVal) * 0.5f;
    } else {
        mkdB = makeupDb();
    }
    const float mkLinear = std::pow(10.f, mkdB / 20.f);
    smoothMakeup_.setTarget(mkLinear);
    smoothMakeup_.trigger();

    // ── Sidechain HPF coefficients ────────────────────────────────
    // Biquad HPF: computed from sideHPF (0-100 → 20-500 Hz)
    // Using 1-pole IIR HPF for simplicity: H(z) = (1+g)/2 * (1 - z^-1) / (1 - ((1-g)/(1+g)) z^-1)
    // We store it as a matched-z first-order HPF: y[n] = a1*x[n] - a1*x[n-1] + b1*y[n-1]
    float hpfA1 = 1.f;  // passthrough by default
    float hpfB1 = 0.f;
    const bool doHPF = params_.sidechain && (params_.sideHPF > 0.f);
    if (doHPF)
    {
        // Map 0-100 → 20-500 Hz (linear)
        const float cutoffHz = 20.f + (params_.sideHPF / 100.f) * 480.f;
        // First-order HPF coefficient via bilinear transform
        // omega_c = 2*pi*f_c / fs, g = tan(omega_c/2)
        // a1 = 1/(1+g), b1 = (1-g)/(1+g)  =>  HPF: y=a1*(x - x_prev) + b1*y_prev
        const float omega = 2.f * 3.14159265358979f * cutoffHz
                            / static_cast<float>(spec_.sampleRate);
        const float g  = std::tan(omega * 0.5f);
        hpfA1 = 1.f / (1.f + g);
        hpfB1 = (1.f - g) / (1.f + g);
    }

    // ── Get raw channel pointers  ─────────────────────────────────
    const float* inL  = inBuf.getReadPointer(0);
    const float* inR  = (inCh > 1) ? inBuf.getReadPointer(1) : inL;
    float*       outL = outBuf.getWritePointer(0);
    float*       outR = (outCh > 1) ? outBuf.getWritePointer(1) : outL;

    // ── Compute input level (RMS over block) ──────────────────────
    const float inLevelDb = computeLevelDb(inBuf, numSamples);

    // Update input level meter with peak ballistic
    const float inLinear = std::pow(10.f, inLevelDb / 20.f);
    if (inLinear > peakInputL_)
        peakInputL_ = inLinear;
    else
        peakInputL_ *= meterReleaseCoeff_;

    inputLevelDb_.store(
        std::max(-96.f, 20.f * std::log10(std::max(peakInputL_, 1e-10f))),
        std::memory_order_relaxed);

    // ── Per-sample processing loop ────────────────────────────────
    float blockPeakOut = 0.f;

    // We use per-sample sidechain path but with block-computed level for
    // the gain reduction to keep the opto character (slow, lazy detection).
    // Attack/release envelope is applied per-sample for accuracy.

    // Target gain in dB for this block (opto: computed from block RMS)
    const float targetGainDb = computeGainDb(inLevelDb, threshDb, ratioVal);

    for (int s = 0; s < numSamples; ++s)
    {
        const float rawL = inL[s];
        const float rawR = inR[s];

        // ── Sidechain HPF (if enabled) ────────────────────────────
        // Apply first-order HPF to the detection signal only
        float scL = rawL;
        float scR = rawR;
        if (doHPF)
        {
            // y[n] = a1*(x[n] - x[n-1]) + b1*y[n-1]
            const float hpfOutL = hpfA1 * (scL - hpfStateL_[0]) + hpfB1 * hpfStateL_[1];
            hpfStateL_[0] = scL;
            hpfStateL_[1] = hpfOutL;
            scL = hpfOutL;

            const float hpfOutR = hpfA1 * (scR - hpfStateR_[0]) + hpfB1 * hpfStateR_[1];
            hpfStateR_[0] = scR;
            hpfStateR_[1] = hpfOutR;
            scR = hpfOutR;
        }

        // ── Opto envelope follower on sidechain ───────────────────
        // Opto characteristic: level detector uses peak envelope on SC signal
        const float scAbs = std::max(std::abs(scL), std::abs(scR));
        if (scAbs > envL_)
            envL_ = attackCoeff_  * envL_ + (1.f - attackCoeff_)  * scAbs;
        else
            envL_ = releaseCoeff_ * envL_ + (1.f - releaseCoeff_) * scAbs;

        // Convert envelope to dB for per-sample gain computation
        const float envDb = (envL_ > 1e-10f)
                            ? 20.f * std::log10(envL_)
                            : -96.f;

        // Per-sample target gain (opto uses per-sample for release tracking)
        const float perSampleTarget = computeGainDb(envDb, threshDb, ratioVal);

        // ── Attack / release on currentGainDb_ ───────────────────
        // Gain reduction is in dB (≤ 0). Attack: faster when gain decreases
        // (more negative). Release: slower when gain recovers (toward 0).
        // Opto: log-domain smoothing of gain reduction.
        if (perSampleTarget < currentGainDb_)
        {
            // Attack: gain is being reduced
            currentGainDb_ = attackCoeff_ * currentGainDb_
                           + (1.f - attackCoeff_) * perSampleTarget;
        }
        else
        {
            // Release: gain is recovering
            currentGainDb_ = releaseCoeff_ * currentGainDb_
                           + (1.f - releaseCoeff_) * perSampleTarget;
        }

        // ── Convert gain reduction to linear ─────────────────────
        const float gainLinear = std::pow(10.f, currentGainDb_ / 20.f);

        // ── Makeup gain (smoothed) ────────────────────────────────
        const float mkup = smoothMakeup_.advance();

        // ── Apply gain to wet signal ──────────────────────────────
        const float wetL = rawL * gainLinear * mkup;
        const float wetR = rawR * gainLinear * mkup;

        // ── Parallel mix (dry / wet blend) ───────────────────────
        outL[s] = rawL * (1.f - mixFrac) + wetL * mixFrac;
        outR[s] = rawR * (1.f - mixFrac) + wetR * mixFrac;

        // Track output peak for metering
        const float outAbs = std::max(std::abs(outL[s]), std::abs(outR[s]));
        if (outAbs > blockPeakOut) blockPeakOut = outAbs;
    }

    // ── Update meters (atomic, relaxed — UI reads these) ─────────
    // Gain reduction: report block-level target (smoother display)
    gainReductionDb_.store(
        std::min(0.f, currentGainDb_),
        std::memory_order_relaxed);

    // Output level with peak ballistic
    if (blockPeakOut > peakOutputL_)
        peakOutputL_ = blockPeakOut;
    else
        peakOutputL_ *= meterReleaseCoeff_;

    outputLevelDb_.store(
        std::max(-96.f, 20.f * std::log10(std::max(peakOutputL_, 1e-10f))),
        std::memory_order_relaxed);
}

// ─────────────────────────────────────────────────────────────────
//  computeLevelDb  — RMS over both channels of a buffer
// ─────────────────────────────────────────────────────────────────
float GrapeCompressor::computeLevelDb(const juce::AudioBuffer<float>& buf,
                                      int numSamples) const noexcept
{
    if (numSamples <= 0) return -96.f;

    const int ch = buf.getNumChannels();
    if (ch <= 0) return -96.f;

    double sumSq = 0.0;
    const int totalSamples = ch * numSamples;

    for (int c = 0; c < ch; ++c)
    {
        const float* data = buf.getReadPointer(c);
        for (int i = 0; i < numSamples; ++i)
        {
            const double s = static_cast<double>(data[i]);
            sumSq += s * s;
        }
    }

    const double rms = std::sqrt(sumSq / static_cast<double>(totalSamples));
    if (rms < 1e-10)  return -96.f;

    const float db = static_cast<float>(20.0 * std::log10(rms));
    return std::max(-96.f, db);
}

// ─────────────────────────────────────────────────────────────────
//  computeGainDb  — opto gain characteristic with soft knee
//
//  Returns gain reduction in dB (≤ 0).
//  Opto character: soft knee blended over 6 dB around threshold.
// ─────────────────────────────────────────────────────────────────
float GrapeCompressor::computeGainDb(float levelDb,
                                     float threshDb,
                                     float ratioVal) const noexcept
{
    constexpr float kKneeDb = 6.f;  // ±3 dB around threshold

    // Below knee entirely: no gain reduction
    if (levelDb <= (threshDb - kKneeDb * 0.5f))
        return 0.f;

    // Hard above-knee gain reduction (opto = ratio applied gently)
    const float hardGainDb = (threshDb - levelDb) * (1.f - 1.f / ratioVal);

    // Within the soft knee region: blend linearly from 0 → hardGainDb
    if (levelDb < (threshDb + kKneeDb * 0.5f))
    {
        // knee blend factor: 0 at lower knee edge, 1 at upper knee edge
        const float blend = (levelDb - (threshDb - kKneeDb * 0.5f)) / kKneeDb;
        // At the upper knee edge, the "hard" value equals the true gain reduction.
        // We blend from 0 toward the hard value as we enter the knee from below.
        const float kneeGainDb = (threshDb - levelDb) * (1.f - 1.f / ratioVal);
        return kneeGainDb * blend * blend;  // quadratic soft knee
    }

    // Fully above knee
    return std::min(0.f, hardGainDb);
}

// ─────────────────────────────────────────────────────────────────
//  applyParameters  — recompute coefficients from params_
//
//  Called from prepare() and setParameter(). NOT called from process().
// ─────────────────────────────────────────────────────────────────
void GrapeCompressor::applyParameters()
{
    const double sr = (spec_.sampleRate > 0.0) ? spec_.sampleRate : 44100.0;

    // Envelope coefficients: exp(-1 / (time_s * sr))
    // Clamped to avoid divide-by-zero on zero-time settings
    const float atkMs  = std::max(0.01f, attackMs());
    const float relMs  = std::max(1.f,   releaseMs());

    attackCoeff_  = std::exp(-1.f / (atkMs  * 0.001f * static_cast<float>(sr)));
    releaseCoeff_ = std::exp(-1.f / (relMs  * 0.001f * static_cast<float>(sr)));

    // Meter release: ~300ms ballistic
    meterReleaseCoeff_ = std::exp(-1.f / (300.f * 0.001f * static_cast<float>(sr)));

    // Clamp coefficients to valid range
    attackCoeff_       = std::clamp(attackCoeff_,       0.f, 0.9999f);
    releaseCoeff_      = std::clamp(releaseCoeff_,      0.f, 0.9999f);
    meterReleaseCoeff_ = std::clamp(meterReleaseCoeff_, 0.f, 0.9999f);
}

// ─────────────────────────────────────────────────────────────────
//  Parameter mapping helpers
// ─────────────────────────────────────────────────────────────────

// threshold: 0 → -60 dB,  100 → 0 dB   (linear map)
float GrapeCompressor::thresholdDb() const noexcept
{
    return -60.f + (params_.threshold / 100.f) * 60.f;
}

// ratio: piecewise — 0→1.0, 50→4.0, 100→60.0 (∞ approximation)
float GrapeCompressor::ratio() const noexcept
{
    const float t = params_.ratio;
    if (t <= 0.f)   return 1.f;
    if (t >= 100.f) return 60.f;

    if (t <= 50.f)
    {
        // 0→1:1,  50→4:1   (linear interpolation)
        return 1.f + (t / 50.f) * 3.f;   // 1 + t/50 * (4-1)
    }
    else
    {
        // 50→4:1,  100→60:1  (linear interpolation)
        const float u = (t - 50.f) / 50.f;
        return 4.f + u * 56.f;            // 4 + u * (60-4)
    }
}

// attack: log map — 0→0.1ms,  100→100ms
float GrapeCompressor::attackMs() const noexcept
{
    // log(100/0.1) = log(1000)
    return 0.1f * std::pow(1000.f, params_.attack / 100.f);
}

// release: log map — 0→10ms,  100→3000ms
float GrapeCompressor::releaseMs() const noexcept
{
    // log(3000/10) = log(300)
    return 10.f * std::pow(300.f, params_.release / 100.f);
}

// makeup: linear — 0→0dB,  100→+18dB
// Meaning of the UI knob:
//   makeupGain == 0 AND autoMakeup == true  → auto (handled in process)
//   makeupGain == 0 AND autoMakeup == false → 0 dB (no makeup)
//   makeupGain == 50 → +9 dB
//   makeupGain == 100 → +18 dB
float GrapeCompressor::makeupDb() const noexcept
{
    return (params_.makeupGain / 100.f) * 18.f;
}

// ─────────────────────────────────────────────────────────────────
//  getParameter / setParameter / getParameterName
// ─────────────────────────────────────────────────────────────────
float GrapeCompressor::getParameter(int idx) const
{
    switch (idx)
    {
    case P_THRESHOLD:   return params_.threshold;
    case P_RATIO:       return params_.ratio;
    case P_ATTACK:      return params_.attack;
    case P_RELEASE:     return params_.release;
    case P_MAKEUP:      return params_.makeupGain;
    case P_AUTO_MAKEUP: return params_.autoMakeup ? 100.f : 0.f;
    case P_SIDECHAIN:   return params_.sidechain  ? 100.f : 0.f;
    case P_SIDE_HPF:    return params_.sideHPF;
    case P_MIX:         return params_.mix;
    default:            return 0.f;
    }
}

void GrapeCompressor::setParameter(int idx, float value)
{
    // Clamp to UI range 0-100 (bool params treat ≥50 as true)
    const float v = std::clamp(value, 0.f, 100.f);

    switch (idx)
    {
    case P_THRESHOLD:   params_.threshold  = v;          break;
    case P_RATIO:       params_.ratio      = v;          break;
    case P_ATTACK:      params_.attack     = v;          break;
    case P_RELEASE:     params_.release    = v;          break;
    case P_MAKEUP:      params_.makeupGain = v;          break;
    case P_AUTO_MAKEUP: params_.autoMakeup = (v >= 50.f); break;
    case P_SIDECHAIN:   params_.sidechain  = (v >= 50.f); break;
    case P_SIDE_HPF:    params_.sideHPF    = v;          break;
    case P_MIX:         params_.mix        = v;          break;
    default: return;
    }

    // Sync to ParameterBank (UI thread write — atomic)
    if (idx >= 0 && idx < P_NUM_PARAMS)
        bank_.setBase(idx, v);

    // Recompute time-domain coefficients
    applyParameters();
}

juce::String GrapeCompressor::getParameterName(int idx) const
{
    static const char* const kNames[P_NUM_PARAMS] = {
        "Threshold",    // P_THRESHOLD
        "Ratio",        // P_RATIO
        "Attack",       // P_ATTACK
        "Release",      // P_RELEASE
        "Makeup Gain",  // P_MAKEUP
        "Auto Makeup",  // P_AUTO_MAKEUP
        "Sidechain",    // P_SIDECHAIN
        "Sidechain HPF",// P_SIDE_HPF
        "Mix",          // P_MIX
    };

    if (idx >= 0 && idx < P_NUM_PARAMS)
        return kNames[idx];

    return "Unknown";
}

} // namespace y2k::dsp
