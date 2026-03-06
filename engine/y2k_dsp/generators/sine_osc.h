#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/generators/sine_osc.h  — v8
// ═══════════════════════════════════════════════════════════════════
#include "../graph/processor_graph.h"
#include "../voice/voice_manager.h"
#include <cmath>

namespace y2k::dsp {

enum SineOscParam : int { SINE_FREQ=0, SINE_GAIN=1 };

class SineOscProcessor final : public ProcessorNode {
public:
    explicit SineOscProcessor(float freqHz=440.f, float gainLin=0.12f) noexcept;

    void setFrequency(float hz)  noexcept;
    void setGain(float lin)      noexcept;
    float getFrequency() const noexcept;
    float getGain()      const noexcept;

    void noteOn (int midiNote, float velocity, int channel=1) noexcept;
    void noteOff(int midiNote, int channel=1) noexcept;
    void allNotesOff() noexcept;

    void prepare(const ProcessSpec&) override;
    void process(const ProcessContext&) override;
    void reset() override;

    int getNumInputChannels()  const override { return 0; }
    int getNumOutputChannels() const override { return 2; }
    int getNumParameters()     const override { return 2; }

    void         setParameter(int i, float v) override;
    float        getParameter(int i)  const   override;
    juce::String getParameterName(int) const  override;

private:
    float  initFreq_, initGain_;
    double phase_       = 0.0;
    double sampleRate_  = 44100.0;
    VoiceManager voices_;
    static constexpr double kTwoPi = 6.283185307179586;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SineOscProcessor)
};

} // namespace y2k::dsp
