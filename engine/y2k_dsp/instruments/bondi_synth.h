#pragma once

// ═══════════════════════════════════════════════════════════════════
//  Y2K DSP — "Bondi" Subtractive Synthesizer
//  Band-limited oscillators + Moog ladder filter + 4-stage ADSR
//  16-voice polyphony with voice stealing.
//  Named after Bondi Blue — the color of the original iMac G3.
// ═══════════════════════════════════════════════════════════════════

#include "../graph/processor_graph.h"
#include <array>
#include <cmath>

namespace y2k::dsp {

// ─────────────────────────────────────────────────────────────────
//  Constants
// ─────────────────────────────────────────────────────────────────
inline constexpr double kTwoPi = 6.283185307179586;
inline constexpr int    kBondiMaxVoices = 16;

// ─────────────────────────────────────────────────────────────────
//  Oscillator type
// ─────────────────────────────────────────────────────────────────
enum class OscType : uint8_t {
    SawY2K    = 0,  // Band-limited saw — slight digital "edge"
    SquarePWM = 1,  // Pulse with variable duty cycle
    SineGlass = 2,  // Sine + harmonic enhancement
    NoisePink = 3,  // Pink noise (for percussion layers)
};

// ─────────────────────────────────────────────────────────────────
//  ADSR Envelope
// ─────────────────────────────────────────────────────────────────
class AdsrEnvelope {
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void prepare(double sampleRate) { sr_ = sampleRate; }

    void noteOn()  { stage_ = Stage::Attack; level_ = 0.f; }
    void noteOff() { if (stage_ != Stage::Idle) stage_ = Stage::Release; }

    // Parameters: all in seconds / linear [0,1]
    float attackSec   = 0.005f;
    float decaySec    = 0.1f;
    float sustainLevel = 0.7f;
    float releaseSec  = 0.3f;

    float advance() noexcept {
        switch (stage_) {
        case Stage::Idle:    return 0.f;
        case Stage::Attack:
            level_ += 1.f / static_cast<float>(sr_ * attackSec);
            if (level_ >= 1.f) { level_ = 1.f; stage_ = Stage::Decay; }
            break;
        case Stage::Decay:
            level_ -= (1.f - sustainLevel) / static_cast<float>(sr_ * decaySec);
            if (level_ <= sustainLevel) { level_ = sustainLevel; stage_ = Stage::Sustain; }
            break;
        case Stage::Sustain:
            break;
        case Stage::Release:
            level_ -= level_ / static_cast<float>(sr_ * releaseSec);
            if (level_ < 1e-5f) { level_ = 0.f; stage_ = Stage::Idle; }
            break;
        }
        return level_;
    }

    bool isIdle()   const { return stage_ == Stage::Idle; }
    float level()   const { return level_; }
    Stage stage()   const { return stage_; }

private:
    double sr_    = 44100.0;
    float  level_ = 0.f;
    Stage  stage_ = Stage::Idle;
};

// ─────────────────────────────────────────────────────────────────
//  Moog Ladder Filter  (Huovilainen model, 4-pole 24dB/oct)
// ─────────────────────────────────────────────────────────────────
class LadderFilter {
public:
    void prepare(double sampleRate) {
        sr_  = sampleRate;
        reset();
    }

    void reset() {
        for (auto& s : stages_) s = 0.f;
        comp_ = 0.f;
    }

    // cutoffHz: 20 – 20000, resonance: 0 – 4 (>1 = self-oscillation)
    void setParameters(float cutoffHz, float resonance) noexcept {
        // Frequency warping for bilinear transform
        const float f  = std::tan(static_cast<float>(kTwoPi) * cutoffHz / (2.f * static_cast<float>(sr_)));
        g_   = f / (1.f + f);
        res_ = juce::jlimit(0.f, 4.f, resonance);
    }

    float processSample(float in) noexcept {
        // Feedback path
        const float fb = res_ * stages_[3];
        float x = in - fb;

        // 4 one-pole stages
        float u;
        u = x;
        stages_[0] = g_ * (u - stages_[0]) + stages_[0];
        u = stages_[0];
        stages_[1] = g_ * (u - stages_[1]) + stages_[1];
        u = stages_[1];
        stages_[2] = g_ * (u - stages_[2]) + stages_[2];
        u = stages_[2];
        stages_[3] = g_ * (u - stages_[3]) + stages_[3];

        return stages_[3];
    }

private:
    double sr_         = 44100.0;
    float  g_          = 0.f;
    float  res_        = 0.f;
    float  comp_       = 0.f;
    std::array<float,4> stages_{};
};

// ─────────────────────────────────────────────────────────────────
//  LFO
// ─────────────────────────────────────────────────────────────────
enum class LfoShape { Sine, Triangle, Square, SampleHold };

class LFO {
public:
    void prepare(double sampleRate) { sr_ = sampleRate; }

    void setRate(float hz)   noexcept { increment_ = static_cast<float>(kTwoPi) * hz / static_cast<float>(sr_); }
    void setDepth(float d)   noexcept { depth_ = d; }
    void setShape(LfoShape s) noexcept { shape_ = s; }

    float advance() noexcept {
        phase_ += increment_;
        if (phase_ > static_cast<float>(kTwoPi)) phase_ -= static_cast<float>(kTwoPi);

        float out = 0.f;
        switch (shape_) {
        case LfoShape::Sine:
            out = std::sin(phase_);
            break;
        case LfoShape::Triangle:
            out = (phase_ < static_cast<float>(kTwoPi) * 0.5f)
                ? phase_ / (static_cast<float>(kTwoPi) * 0.5f) * 2.f - 1.f
                : 1.f - (phase_ - static_cast<float>(kTwoPi) * 0.5f) / (static_cast<float>(kTwoPi) * 0.5f) * 2.f;
            break;
        case LfoShape::Square:
            out = phase_ < static_cast<float>(kTwoPi) * 0.5f ? 1.f : -1.f;
            break;
        case LfoShape::SampleHold:
            // S&H: change value at phase reset
            break;
        }
        return out * depth_;
    }

private:
    double    sr_        = 44100.0;
    float     phase_     = 0.f;
    float     increment_ = 0.f;
    float     depth_     = 0.f;
    LfoShape  shape_     = LfoShape::Sine;
};

// ─────────────────────────────────────────────────────────────────
//  BondiVoice  –  single synthesizer voice
// ─────────────────────────────────────────────────────────────────
class BondiVoice {
public:
    void prepare(double sampleRate) {
        sr_ = sampleRate;
        env_.prepare(sampleRate);
        filter_.prepare(sampleRate);
        lfo_.prepare(sampleRate);
    }

    void noteOn(int midiNote, float velocity) {
        midiNote_  = midiNote;
        velocity_  = velocity;
        frequency_ = 440.f * std::pow(2.f, (midiNote - 69) / 12.f);
        phase_     = 0.f;
        age_       = 0;
        env_.noteOn();
        isActive_  = true;
    }

    void noteOff() { env_.noteOff(); }
    bool isIdle()  const { return !isActive_ || env_.isIdle(); }
    int  note()    const { return midiNote_; }
    int  age()     const { return age_; }

    // Parameters (set from BondiSynth before processing)
    OscType  oscType  = OscType::SawY2K;
    float    dutyCycle = 0.5f;     // For PWM (0.1 – 0.9)
    float    filterCutoff = 1000.f; // Hz
    float    filterRes    = 0.5f;
    float    lfoRate      = 2.f;
    float    lfoDepth     = 0.f;
    float    pitchBend    = 0.f;   // Semitones

    // Process one sample
    float processSample() noexcept {
        ++age_;

        // Envelope
        const float envLevel = env_.advance();
        if (env_.isIdle()) { isActive_ = false; return 0.f; }

        // LFO
        lfo_.setRate(lfoRate);
        lfo_.setDepth(lfoDepth);
        const float lfoOut = lfo_.advance();

        // Oscillator
        const float freq  = frequency_ * std::pow(2.f, (pitchBend + lfoOut * 0.1f) / 12.f);
        const float phInc = static_cast<float>(kTwoPi) * freq / static_cast<float>(sr_);
        phase_ += phInc;
        if (phase_ > static_cast<float>(kTwoPi)) phase_ -= static_cast<float>(kTwoPi);

        float osc = generateOscillator(phase_, freq);

        // Filter (LFO modulates cutoff)
        const float modCutoff = juce::jlimit(20.f, 20000.f,
                                              filterCutoff * (1.f + lfoOut * 0.5f));
        filter_.setParameters(modCutoff, filterRes * 3.5f);
        osc = filter_.processSample(osc);

        return osc * envLevel * velocity_;
    }

private:
    float generateOscillator(float phase, float freq) const noexcept {
        switch (oscType) {
        case OscType::SawY2K: {
            // Additive band-limited saw: sum harmonics up to Nyquist
            float out   = 0.f;
            float level = 1.f;
            for (float h = 1.f; h * freq < static_cast<float>(sr_) * 0.5f; h += 1.f, level *= 0.8f) {
                out += std::sin(phase * h) * level;
            }
            return out * 0.5f;
        }
        case OscType::SquarePWM: {
            // Phase-distorted square with PWM
            const float threshold = static_cast<float>(kTwoPi) * dutyCycle;
            return phase < threshold ? 1.f : -1.f;
        }
        case OscType::SineGlass:
            // Sine + soft 2nd harmonic for "glass" character
            return std::sin(phase) + 0.15f * std::sin(2.f * phase);
        case OscType::NoisePink:
            // Simple white noise (pink filtering done in BondiSynth)
            return (static_cast<float>(rand()) / RAND_MAX) * 2.f - 1.f;
        }
        return 0.f;
    }

    double       sr_        = 44100.0;
    float        frequency_ = 440.f;
    float        phase_     = 0.f;
    float        velocity_  = 1.f;
    int          midiNote_  = 60;
    int          age_       = 0;
    bool         isActive_  = false;
    AdsrEnvelope env_;
    LadderFilter filter_;
    LFO          lfo_;
};

// ─────────────────────────────────────────────────────────────────
//  BondiSynth::Parameters
// ─────────────────────────────────────────────────────────────────
struct BondiParameters {
    // Y2K UI: all knobs are 0–100 for friendly presentation
    // Internal mapping is performed in BondiSynth::applyParameters()

    // Oscillator
    float oscType       = 0.f;   // 0=Saw, 25=Square, 50=Sine, 75=Noise
    float oscMix        = 50.f;  // Osc A/B mix

    // Filter
    float filterCutoff  = 70.f;  // 0→20Hz, 100→20kHz (log)
    float filterRes     = 20.f;  // 0→0, 100→4.0 (self-oscillation)

    // Envelope (0→1ms, 100→10s log)
    float envAttack     = 10.f;
    float envDecay      = 30.f;
    float envSustain    = 70.f;
    float envRelease    = 40.f;

    // LFO (0→0.1Hz, 100→100Hz log)
    float lfoRate       = 30.f;
    float lfoDepth      = 0.f;
    float lfoShape      = 0.f;   // 0=Sine, 33=Tri, 66=Square, 100=S&H

    // Pitch
    float pitchSemitones = 0.f;  // -24 to +24
    float dutyCycle      = 50.f; // PWM width (%)

    // Voice
    float portamentoTime = 0.f;  // 0→off, 100→2s glide
    float volume         = 80.f;
};

// ─────────────────────────────────────────────────────────────────
//  BondiSynth  –  polyphonic subtractive synthesizer node
// ─────────────────────────────────────────────────────────────────
class BondiSynth : public ProcessorNode {
public:
    BondiSynth();

    // ── ProcessorNode interface ───────────────────────────────────
    void prepare(const ProcessSpec& spec) override;
    void process(const ProcessContext& ctx) override;
    void reset() override;

    int getNumInputChannels()  const override { return 0; }  // MIDI-only input
    int getNumOutputChannels() const override { return 2; }

    // ── Parameter access ─────────────────────────────────────────
    int   getNumParameters() const override;
    float getParameter(int idx) const override;
    void  setParameter(int idx, float value) override;
    juce::String getParameterName(int idx) const override;

    // ── Direct parameter block (for UI binding) ───────────────────
    BondiParameters& params() { return params_; }
    const BondiParameters& params() const { return params_; }

    // ── Voice info (for UI animation) ────────────────────────────
    int getActiveVoiceCount() const noexcept;
    int getVoiceNote(int voiceIndex) const noexcept;

private:
    void handleMidi(const juce::MidiBuffer& midi);
    void noteOn(int channel, int note, float velocity);
    void noteOff(int channel, int note);
    void allNotesOff();
    void applyParameters();   // Maps 0-100 UI params → DSP units

    // Voice stealing: returns index of voice to steal
    int findFreeVoice() const noexcept;
    int findVoiceForNote(int note) const noexcept;

    std::array<BondiVoice, kBondiMaxVoices> voices_;
    BondiParameters params_;
    ProcessSpec     spec_;
    float           masterVolume_ = 0.8f;

    // Smoothed parameters for zipper-free changes
    SmoothParameter smoothCutoff_;
    SmoothParameter smoothRes_;
    SmoothParameter smoothVolume_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BondiSynth)
};

} // namespace y2k::dsp
