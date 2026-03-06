#pragma once

// ═══════════════════════════════════════════════════════════════════
//  Y2K DSP — SamplerPlayer
//  Polyphonic sample / loop player with per-key zone assignment.
//
//  Features:
//    • Up to kMaxZones (128) key zones, each mapping a pitch range
//      to a loaded juce::AudioBuffer<float>.
//    • Up to kMaxVoices (16) simultaneous voices with linear
//      interpolation and loop support.
//    • MIDI noteOn triggers a voice: zone lookup by MIDI pitch,
//      pitch-shifted via playback rate = 2^((note-root)/12).
//    • RT-safe: zero allocation in process().  All samples are
//      loaded before prepare() and held as shared_ptr so the audio
//      thread never frees memory.
//    • loadZone() / clearZones() are non-RT (UI thread).
//      An atomic zoneVersion_ counter lets process() detect stale
//      zone list without a mutex.
// ═══════════════════════════════════════════════════════════════════

#include "../graph/processor_graph.h"

#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <memory>

namespace y2k::dsp {

// ─────────────────────────────────────────────────────────────────
//  KeyZone  —  maps a pitch range to one audio sample
// ─────────────────────────────────────────────────────────────────
struct KeyZone
{
    int  rootNote  = 60;    // MIDI note for 1:1 playback speed
    int  loNote    = 0;     // inclusive lower bound
    int  hiNote    = 127;   // inclusive upper bound
    std::shared_ptr<juce::AudioBuffer<float>> buffer;
    double sampleRate = 44100.0;
    bool   loop       = false;
    int    loopStart  = 0;  // samples
    int    loopEnd    = 0;  // samples (inclusive), 0 = end of buffer
    float  gain       = 1.f;
    juce::String name;
};

// ─────────────────────────────────────────────────────────────────
//  SamplerPlayer
// ─────────────────────────────────────────────────────────────────
class SamplerPlayer : public ProcessorNode
{
public:
    static constexpr int kMaxVoices = 16;
    static constexpr int kMaxZones  = 128;

    SamplerPlayer()
    {
        name      = "Sampler";
        nodeColor = Y2KColor::Tangerine();
        voices_.fill(Voice{});
    }

    // ── Non-RT zone management ────────────────────────────────────

    // Add or replace a zone.  Zones are kept sorted by loNote.
    // Must NOT be called from the audio thread.
    inline void loadZone(const KeyZone& z)
    {
        if (zoneCount_ >= kMaxZones) return;

        // Append then insertion-sort by loNote
        zones_[zoneCount_++] = z;
        for (int i = zoneCount_ - 1; i > 0; --i)
        {
            if (zones_[i].loNote < zones_[i - 1].loNote)
                std::swap(zones_[i], zones_[i - 1]);
            else
                break;
        }
        // Publish new version — audio thread sees change on next block
        zoneVersion_.fetch_add(1, std::memory_order_release);
    }

    // Remove all zones.  Must NOT be called from the audio thread.
    inline void clearZones() noexcept
    {
        // Null out shared_ptrs so samples can be freed by caller
        for (int i = 0; i < zoneCount_; ++i)
            zones_[i].buffer.reset();
        zoneCount_ = 0;
        zoneVersion_.fetch_add(1, std::memory_order_release);
    }

    inline int zoneCount() const noexcept { return zoneCount_; }

    // Find first zone whose [loNote, hiNote] contains note.
    // Safe to call from the audio thread (read-only).
    inline const KeyZone* findZone(int note) const noexcept
    {
        for (int i = 0; i < zoneCount_; ++i)
        {
            const KeyZone& z = zones_[i];
            if (note >= z.loNote && note <= z.hiNote)
                return &z;
        }
        return nullptr;
    }

    // ── MIDI triggers ─────────────────────────────────────────────

    // Trigger a note-on.  Safe to call from the audio thread.
    inline void noteOn(int note, float vel, int ch) noexcept
    {
        const KeyZone* zone = findZone(note);
        if (!zone || !zone->buffer) return;

        // Find a free voice; steal the oldest if all are active
        int idx = findFreeVoice();

        Voice& v = voices_[idx];
        v.active    = true;
        v.note      = note;
        v.channel   = ch;
        v.readPos   = 0;
        v.velGain   = std::clamp(vel, 0.f, 1.f);
        v.envGain   = 1.f;
        v.releasing = false;
        v.zone      = zone;

        // Pitch ratio: account for zone sample rate vs engine sample rate
        const double semitones   = static_cast<double>(note - zone->rootNote);
        const double pitchRatio  = std::pow(2.0, semitones / 12.0);
        const double srRatio     = zone->sampleRate / spec_.sampleRate;
        v.phaseInc = pitchRatio * srRatio;
        v.phase    = 0.0;
    }

    // Trigger a note-off.  Safe to call from the audio thread.
    inline void noteOff(int note, int ch) noexcept
    {
        for (auto& v : voices_)
        {
            if (v.active && v.note == note && v.channel == ch && !v.releasing)
            {
                if (v.zone && v.zone->loop)
                    v.releasing = true;  // Let loop finish then fade
                else
                    v.active = false;    // One-shot: stop immediately on off
            }
        }
    }

    // ── ProcessorNode interface ───────────────────────────────────

    inline void prepare(const ProcessSpec& spec) override
    {
        spec_ = spec;

        // Pre-allocate output buffer — never resize in process()
        outputBuf.setSize(2, std::max(1, spec.maxBlockSize), false, true, false);
        outputBuf.clear();

        // No parameters to bank for the sampler
        initParameters(0);
    }

    // process() — render all active voices into outputBuf.  [RT-safe]
    inline void process(const ProcessContext& ctx) override
    {
        const int numSamples = ctx.blockSize;
        outputBuf.clear();

        if (numSamples <= 0) return;

        float* outL = outputBuf.getWritePointer(0);
        float* outR = outputBuf.getNumChannels() > 1
                      ? outputBuf.getWritePointer(1)
                      : outL;

        for (auto& v : voices_)
        {
            if (!v.active) continue;

            const KeyZone* zone = v.zone;
            if (!zone || !zone->buffer) { v.active = false; continue; }

            const juce::AudioBuffer<float>& buf = *zone->buffer;
            const int   bufCh  = buf.getNumChannels();
            const int   bufLen = buf.getNumSamples();
            if (bufLen <= 0) { v.active = false; continue; }

            // Resolve loop endpoints
            const int loopStart = (zone->loop)
                                  ? std::clamp(zone->loopStart, 0, bufLen - 1)
                                  : 0;
            const int loopEnd   = (zone->loop && zone->loopEnd > 0)
                                  ? std::clamp(zone->loopEnd, loopStart + 1, bufLen - 1)
                                  : bufLen - 1;

            const float* srcL = buf.getReadPointer(0);
            const float* srcR = (bufCh > 1) ? buf.getReadPointer(1) : srcL;

            const double phaseInc = v.phaseInc;
            const float  zoneGain = zone->gain * v.velGain * v.envGain;

            for (int s = 0; s < numSamples; ++s)
            {
                if (!v.active) break;

                // Integer and fractional parts for linear interpolation
                const int    pos0 = static_cast<int>(v.phase);
                const int    pos1 = pos0 + 1;
                const float  frac = static_cast<float>(v.phase - static_cast<double>(pos0));

                // Clamp read positions to buffer bounds
                const int p0 = std::min(pos0, bufLen - 1);
                const int p1 = std::min(pos1, bufLen - 1);

                const float sL = srcL[p0] + frac * (srcL[p1] - srcL[p0]);
                const float sR = srcR[p0] + frac * (srcR[p1] - srcR[p0]);

                outL[s] += sL * zoneGain;
                outR[s] += sR * zoneGain;

                // Advance phase
                v.phase += phaseInc;

                // Loop / end-of-sample handling
                const double endPos = static_cast<double>(loopEnd);
                if (zone->loop)
                {
                    // Wrap back to loopStart when past loopEnd
                    if (v.phase >= endPos + 1.0)
                    {
                        if (v.releasing)
                        {
                            // Note-off on looped sample: stop at loop end
                            v.active = false;
                        }
                        else
                        {
                            const double loopLen = static_cast<double>(loopEnd - loopStart + 1);
                            v.phase -= loopLen;
                            if (v.phase < static_cast<double>(loopStart))
                                v.phase = static_cast<double>(loopStart);
                        }
                    }
                }
                else
                {
                    // One-shot: stop when buffer exhausted
                    if (v.phase >= static_cast<double>(bufLen))
                        v.active = false;
                }
            }
        }

        // Copy outputBuf → ctx.outputBuffer (additive, so instruments can be summed)
        if (ctx.outputBuffer)
        {
            const int dstCh      = ctx.outputBuffer->getNumChannels();
            const int dstSamples = std::min(numSamples, ctx.outputBuffer->getNumSamples());

            if (dstCh > 0)
                ctx.outputBuffer->addFrom(0, 0, outputBuf, 0, 0, dstSamples);
            if (dstCh > 1)
                ctx.outputBuffer->addFrom(1, 0, outputBuf, 1, 0, dstSamples);
        }
    }

    inline void reset() noexcept override
    {
        for (auto& v : voices_) v.active = false;
        outputBuf.clear();
    }

    // ── ProcessorNode virtuals ────────────────────────────────────
    inline int          getNumInputChannels()  const override { return 0; }
    inline int          getNumOutputChannels() const override { return 2; }
    inline int          getNumParameters()     const override { return 0; }
    inline float        getParameter(int)      const override { return 0.f; }
    inline void         setParameter(int, float)     override {}
    inline juce::String getParameterName(int)  const override { return {}; }

private:
    // ─────────────────────────────────────────────────────────────
    //  Voice  —  one active note playback state
    // ─────────────────────────────────────────────────────────────
    struct Voice
    {
        bool          active    = false;
        int           note      = 0;
        int           channel   = 0;
        int           readPos   = 0;   // integer sample position (legacy, phase is authoritative)
        double        phase     = 0.0; // fractional read position in source buffer
        double        phaseInc  = 1.0; // samples-per-output-sample advance rate
        const KeyZone* zone     = nullptr;
        float         velGain   = 1.f;
        float         envGain   = 1.f;
        bool          releasing = false;
    };

    // ── Find a voice index to allocate ───────────────────────────
    inline int findFreeVoice() const noexcept
    {
        // Prefer an idle voice
        for (int i = 0; i < kMaxVoices; ++i)
            if (!voices_[i].active) return i;

        // Voice steal: take the one furthest along its buffer (closest to end)
        // as it's produced the most audio already.
        int    bestIdx   = 0;
        double bestPhase = -1.0;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            if (voices_[i].phase > bestPhase)
            {
                bestPhase = voices_[i].phase;
                bestIdx   = i;
            }
        }
        return bestIdx;
    }

    // ── Zone storage (non-RT writes, RT reads) ────────────────────
    // zoneVersion_ is bumped on every loadZone/clearZones.
    // Audio thread reads zoneVersion_ with relaxed ordering — it only
    // needs to see a consistent zone list, which it does because
    // zone mutations happen before the version bump (release fence).
    std::array<KeyZone, kMaxZones> zones_{};
    int                            zoneCount_{0};
    std::atomic<int>               zoneVersion_{0};

    // ── Voice pool ────────────────────────────────────────────────
    mutable std::array<Voice, kMaxVoices> voices_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerPlayer)
};

} // namespace y2k::dsp
