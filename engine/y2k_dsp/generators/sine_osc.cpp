// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/generators/sine_osc.cpp  — v8
// ═══════════════════════════════════════════════════════════════════
#include "sine_osc.h"
#include <algorithm>

namespace y2k::dsp {

SineOscProcessor::SineOscProcessor(float freqHz, float gainLin) noexcept
    : initFreq_(freqHz), initGain_(gainLin)
{
    name = "SineOsc";
    nodeColor = Y2KColor::Lime();
}

void SineOscProcessor::prepare(const ProcessSpec& spec) {
    spec_       = spec;
    sampleRate_ = spec.sampleRate;
    phase_      = 0.0;

    initParameters(2);
    bank_.setRange(SINE_FREQ, 20.f, 20000.f);
    bank_.setRange(SINE_GAIN,  0.f,     1.f);
    bank_.setBase (SINE_FREQ, initFreq_);
    bank_.setBase (SINE_GAIN, initGain_);

    voices_.prepare(spec);

    outputBuf.setSize(2, std::max(1, spec.maxBlockSize), false, true, false);
    outputBuf.clear();
}

void SineOscProcessor::reset() {
    phase_ = 0.0;
    voices_.allNotesOff();
    outputBuf.clear();
}

void SineOscProcessor::setFrequency(float hz) noexcept { bank_.setBase(SINE_FREQ, std::clamp(hz,20.f,20000.f)); }
void SineOscProcessor::setGain(float lin)      noexcept { bank_.setBase(SINE_GAIN, std::clamp(lin,0.f,1.f));    }
float SineOscProcessor::getFrequency() const noexcept   { return bank_.getBase(SINE_FREQ); }
float SineOscProcessor::getGain()      const noexcept   { return bank_.getBase(SINE_GAIN); }

void SineOscProcessor::noteOn (int note, float vel, int ch) noexcept { voices_.noteOn(note, vel, ch);  }
void SineOscProcessor::noteOff(int note, int ch) noexcept           { voices_.noteOff(note, ch);      }
void SineOscProcessor::allNotesOff() noexcept                       { voices_.allNotesOff();           }

void SineOscProcessor::process(const ProcessContext& ctx) {
    const int N = ctx.blockSize;
    if (N <= 0) return;

    // Resolved = base + automation offset + midi offset
    const float freq = bank_.resolved(SINE_FREQ);
    const float gain = bank_.resolved(SINE_GAIN);

    float* L = outputBuf.getWritePointer(0);
    float* R = outputBuf.getWritePointer(1);

    // Direct proof-of-life sine tone
    const double inc = kTwoPi * double(freq) / sampleRate_;
    double phi = phase_;
    for (int s = 0; s < N; ++s) {
        const float smp = float(std::sin(phi)) * gain;
        L[s] = smp;
        R[s] = smp;
        phi += inc;
        if (phi >= kTwoPi) phi -= kTwoPi;
    }
    phase_ = phi;

    // VoiceManager polyphonic layer (adds on top of direct tone)
    if (voices_.activeVoiceCount() > 0)
        voices_.renderBlock(outputBuf, N);

    // MIDI routing note: SineOscProcessor does NOT consume midiBuffer here.
    // It is driven exclusively via explicit noteOn()/noteOff() API calls from
    // AudioEngine (Kontrol panel events + lock-free MIDI queue).
    // BondiSynth consumes the shared midiBuffer for clip-scheduled playback.
    // Consuming it here would cause double-triggering on every MIDI event.
}

void SineOscProcessor::setParameter(int i, float v) { bank_.setBase(i, v); }
float SineOscProcessor::getParameter(int i) const    { return bank_.getBase(i); }
juce::String SineOscProcessor::getParameterName(int i) const {
    if (i==SINE_FREQ) return "Frequency (Hz)";
    if (i==SINE_GAIN) return "Gain (linear)";
    return {};
}

} // namespace y2k::dsp
