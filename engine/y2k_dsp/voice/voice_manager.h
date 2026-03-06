#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/voice/voice_manager.h  — v8
//
//  VoiceManager — fixed-pool polyphonic voice engine.
//
//  Design rules:
//    • Fixed pool of kMaxVoices voices — allocated at prepare() time.
//    • noteOn / noteOff — RT-safe, no allocation, no lock.
//    • Voice stealing: oldest active voice.
//    • Each voice has its own ADSR envelope and oscillator phase.
//    • renderBlock() called per audio block — accumulates into output buffer.
//    • Zero-allocation after prepare().
//
//  Usage:
//    VoiceManager vm;
//    vm.prepare(spec);
//    vm.noteOn(60, 1.0f, 1);   // note, vel, channel
//    ...
//    vm.renderBlock(outBuf, nSamples);
// ═══════════════════════════════════════════════════════════════════

#include "../graph/processor_graph.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace y2k::dsp {

// ─────────────────────────────────────────────────────────────────
//  ADSR Envelope  [inline, no allocation]
// ─────────────────────────────────────────────────────────────────
struct ADSR {
    enum class Stage : uint8_t { Idle, Attack, Decay, Sustain, Release };

    float attackSec    = 0.005f;
    float decaySec     = 0.10f;
    float sustainLevel = 0.70f;
    float releaseSec   = 0.30f;

    Stage stage  = Stage::Idle;
    float level  = 0.f;
    double sr    = 44100.0;

    bool isActive() const noexcept { return stage != Stage::Idle; }

    void reset()   noexcept { stage = Stage::Idle; level = 0.f; }
    void noteOn()  noexcept { stage = Stage::Attack; level = 0.f; }
    void noteOff() noexcept { if (stage != Stage::Idle) stage = Stage::Release; }

    // Returns one envelope sample. Call once per output sample.
    float advance() noexcept {
        switch (stage) {
        case Stage::Idle:    return 0.f;
        case Stage::Attack:
            level += 1.f / float(sr * attackSec);
            if (level >= 1.f) { level = 1.f; stage = Stage::Decay; }
            break;
        case Stage::Decay:
            level -= (1.f - sustainLevel) / float(sr * decaySec);
            if (level <= sustainLevel) { level = sustainLevel; stage = Stage::Sustain; }
            break;
        case Stage::Sustain:
            level = sustainLevel;
            break;
        case Stage::Release:
            level -= sustainLevel / float(sr * releaseSec);
            if (level <= 0.f) { level = 0.f; stage = Stage::Idle; }
            break;
        }
        return level;
    }
};

// ─────────────────────────────────────────────────────────────────
//  Voice  [stack-allocated inside VoiceManager]
// ─────────────────────────────────────────────────────────────────
struct Voice {
    static constexpr double kTwoPi = 6.283185307179586;

    int    note      = -1;
    int    channel   = 0;
    float  velocity  = 0.f;
    bool   active    = false;
    int    age       = 0;     // incremented on noteOn, used for stealing

    double phase     = 0.0;
    double phaseInc  = 0.0;   // set by noteOn
    double sr        = 44100.0;
    ADSR   env;

    void prepare(double sampleRate) noexcept {
        sr = sampleRate;
        env.sr = sampleRate;
    }

    void noteOn(int midiNote, float vel, int ch) noexcept {
        note     = midiNote;
        channel  = ch;
        velocity = vel;
        active   = true;
        // MIDI note → Hz: A4=69→440Hz
        const double freq = 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
        phaseInc = kTwoPi * freq / sr;
        phase    = 0.0;
        env.noteOn();
    }

    void noteOff() noexcept {
        env.noteOff();
        // active remains true until ADSR reaches Idle
    }

    // Render one sample — no branches other than ADSR stage
    float renderSample() noexcept {
        const float envVal = env.advance();
        if (!env.isActive()) {
            active = false;
            note   = -1;
            return 0.f;
        }
        const float s = float(std::sin(phase)) * velocity * envVal;
        phase += phaseInc;
        if (phase >= kTwoPi) phase -= kTwoPi;
        return s;
    }
};

// ─────────────────────────────────────────────────────────────────
//  VoiceManager
// ─────────────────────────────────────────────────────────────────
class VoiceManager {
public:
    static constexpr int kMaxVoices = 16;

    VoiceManager() = default;

    // Non-RT: call once with device spec
    void prepare(const ProcessSpec& spec) noexcept {
        sampleRate_ = spec.sampleRate;
        for (auto& v : voices_) v.prepare(sampleRate_);
        ageCounter_ = 0;
    }

    // ── RT: note events (no allocation) ──────────────────────

    // noteOn: find free or steal oldest voice
    void noteOn(int midiNote, float velocity, int channel=1) noexcept {
        // First: if note already playing (same channel), retrigger it
        for (auto& v : voices_) {
            if (v.active && v.note==midiNote && v.channel==channel) {
                v.noteOn(midiNote, velocity, channel);
                v.age = ++ageCounter_;
                return;
            }
        }
        // Find free voice
        Voice* target = nullptr;
        for (auto& v : voices_) {
            if (!v.active) { target = &v; break; }
        }
        // Voice steal: oldest
        if (!target) {
            int oldest = -1;
            for (auto& v : voices_) {
                if (v.age < oldest || oldest==-1) { oldest=v.age; target=&v; }
            }
        }
        if (target) {
            target->noteOn(midiNote, velocity, channel);
            target->age = ++ageCounter_;
        }
    }

    // noteOff: release all matching voices on that channel
    void noteOff(int midiNote, int channel=1) noexcept {
        for (auto& v : voices_) {
            if (v.active && v.note==midiNote && (channel==0||v.channel==channel))
                v.noteOff();
        }
    }

    void allNotesOff() noexcept {
        for (auto& v : voices_) v.noteOff();
    }

    // ── RT: render [no allocation] ────────────────────────────
    //  Accumulates into outBuf (does not clear). Call clear() before.
    void renderBlock(juce::AudioBuffer<float>& outBuf, int numSamples) noexcept {
        const int numCh = outBuf.getNumChannels();
        if (numCh <= 0 || numSamples <= 0) return;

        for (auto& v : voices_) {
            if (!v.active) continue;
            float* L = outBuf.getWritePointer(0);
            float* R = numCh > 1 ? outBuf.getWritePointer(1) : nullptr;
            for (int s = 0; s < numSamples; ++s) {
                const float smp = v.renderSample();
                L[s] += smp;
                if (R) R[s] += smp;
            }
        }
    }

    // UI thread: ADSR parameter setters (applied to all current and future voices)
    void setAttack (float s) noexcept { for(auto& v:voices_) v.env.attackSec   =s; adsrA_=s; }
    void setDecay  (float s) noexcept { for(auto& v:voices_) v.env.decaySec    =s; adsrD_=s; }
    void setSustain(float l) noexcept { for(auto& v:voices_) v.env.sustainLevel=l; adsrS_=l; }
    void setRelease(float s) noexcept { for(auto& v:voices_) v.env.releaseSec  =s; adsrR_=s; }

    int activeVoiceCount() const noexcept {
        int n=0; for(const auto& v:voices_) if(v.active) ++n; return n;
    }

private:
    std::array<Voice, kMaxVoices> voices_{};
    double  sampleRate_ = 44100.0;
    int     ageCounter_ = 0;

    // Stored ADSR defaults for new voices
    float adsrA_=0.005f, adsrD_=0.10f, adsrS_=0.70f, adsrR_=0.30f;
};

} // namespace y2k::dsp
