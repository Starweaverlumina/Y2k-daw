// ═══════════════════════════════════════════════════════════════════
//  Y2K DSP — "Bondi" Synthesizer Implementation
// ═══════════════════════════════════════════════════════════════════

#include "bondi_synth.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace y2k::dsp {

// ─────────────────────────────────────────────────────────────────
//  Parameter indices  (must match getParameterName() order)
// ─────────────────────────────────────────────────────────────────
enum ParamIdx : int {
    P_OSC_TYPE = 0,
    P_FILTER_CUTOFF,
    P_FILTER_RES,
    P_ENV_ATTACK,
    P_ENV_DECAY,
    P_ENV_SUSTAIN,
    P_ENV_RELEASE,
    P_LFO_RATE,
    P_LFO_DEPTH,
    P_LFO_SHAPE,
    P_PITCH,
    P_DUTY,
    P_PORTAMENTO,
    P_VOLUME,
    P_NUM_PARAMS
};

// ─────────────────────────────────────────────────────────────────
//  Mapping helpers  (0-100 UI range → DSP units)
// ─────────────────────────────────────────────────────────────────
static float mapLog(float v01, float lo, float hi) {
    return lo * std::pow(hi / lo, v01);
}

static float mapLinear(float v01, float lo, float hi) {
    return lo + v01 * (hi - lo);
}

// ─────────────────────────────────────────────────────────────────
//  BondiSynth
// ─────────────────────────────────────────────────────────────────
BondiSynth::BondiSynth() {
    name      = "Bondi";
    nodeColor = Y2KColor::BondiBlue();
}

void BondiSynth::prepare(const ProcessSpec& spec) {
    spec_ = spec;
    for (auto& v : voices_) v.prepare(spec.sampleRate);

    smoothCutoff_.reset(spec.sampleRate, 20.0);
    smoothRes_.reset(spec.sampleRate, 20.0);
    smoothVolume_.reset(spec.sampleRate, 5.0);

    // Initialise ParameterBank (v8 unified param authority)
    initParameters(P_NUM_PARAMS);
    for (int i = 0; i < P_NUM_PARAMS; ++i) {
        bank_.setRange(i, 0.f, 100.f);
        bank_.setBase(i, getParameter(i));  // seed from existing defaults
    }

    outputBuf.setSize(2, std::max(1, spec.maxBlockSize), false, true, false);
    outputBuf.clear();
}

void BondiSynth::reset() {
    for (auto& v : voices_) {
        v.noteOff();
    }
    outputBuf.clear();
}

void BondiSynth::process(const ProcessContext& ctx) {
    const int numSamples = ctx.blockSize;
    // DO NOT call setSize here — outputBuf is pre-sized in prepare().
    // keepExistingContent=true, clearExtraSpace=false, avoidReallocating=true
    outputBuf.clear();

    // Apply any pending parameter changes
    applyParameters();

    // Handle MIDI events
    if (ctx.midiBuffer) handleMidi(*ctx.midiBuffer);

    // Process all active voices
    auto* leftData  = outputBuf.getWritePointer(0);
    auto* rightData = outputBuf.getWritePointer(1);

    for (int s = 0; s < numSamples; ++s) {
        const float vol = smoothVolume_.advance();
        float mixed     = 0.f;

        for (auto& voice : voices_) {
            if (!voice.isIdle()) {
                mixed += voice.processSample();
            }
        }

        // Soft clip master output
        mixed = std::tanh(mixed * 0.5f) * vol;

        leftData[s]  = mixed;
        rightData[s] = mixed;
    }
}

// ─────────────────────────────────────────────────────────────────
void BondiSynth::handleMidi(const juce::MidiBuffer& midi) {
    for (const auto meta : midi) {
        const auto msg = meta.getMessage();
        if      (msg.isNoteOn())  noteOn(msg.getChannel(), msg.getNoteNumber(), msg.getVelocity() / 127.f);
        else if (msg.isNoteOff()) noteOff(msg.getChannel(), msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) allNotesOff();
    }
}

void BondiSynth::noteOn(int /*channel*/, int note, float velocity) {
    // Check if already playing this note — retrigger
    int idx = findVoiceForNote(note);
    if (idx < 0) idx = findFreeVoice();

    voices_[idx].noteOn(note, velocity);
}

void BondiSynth::noteOff(int /*channel*/, int note) {
    const int idx = findVoiceForNote(note);
    if (idx >= 0) voices_[idx].noteOff();
}

void BondiSynth::allNotesOff() {
    for (auto& v : voices_) v.noteOff();
}

// ─────────────────────────────────────────────────────────────────
int BondiSynth::findFreeVoice() const noexcept {
    // 1) Find an idle voice
    for (int i = 0; i < kBondiMaxVoices; ++i)
        if (voices_[i].isIdle()) return i;

    // 2) Steal the oldest active voice (release phase preferred)
    int   oldest     = 0;
    int   maxAge     = -1;
    for (int i = 0; i < kBondiMaxVoices; ++i) {
        if (voices_[i].age() > maxAge) {
            maxAge = voices_[i].age();
            oldest = i;
        }
    }
    return oldest;
}

int BondiSynth::findVoiceForNote(int note) const noexcept {
    for (int i = 0; i < kBondiMaxVoices; ++i)
        if (!voices_[i].isIdle() && voices_[i].note() == note) return i;
    return -1;
}

// ─────────────────────────────────────────────────────────────────
void BondiSynth::applyParameters() {
    // Read via ParameterBank resolved() so automation/MIDI layers contribute.
    // Falls back to params_ defaults if bank_ not yet initialised.
    const float oscTypeVal     = bank_.size() > 0 ? bank_.resolved(P_OSC_TYPE)     : params_.oscType;
    const float cutoffVal      = bank_.size() > 0 ? bank_.resolved(P_FILTER_CUTOFF): params_.filterCutoff;
    const float resVal         = bank_.size() > 0 ? bank_.resolved(P_FILTER_RES)   : params_.filterRes;
    const float volumeVal      = bank_.size() > 0 ? bank_.resolved(P_VOLUME)       : params_.volume;
    const float attackVal      = bank_.size() > 0 ? bank_.resolved(P_ENV_ATTACK)   : params_.envAttack;
    const float decayVal       = bank_.size() > 0 ? bank_.resolved(P_ENV_DECAY)    : params_.envDecay;
    const float sustainVal     = bank_.size() > 0 ? bank_.resolved(P_ENV_SUSTAIN)  : params_.envSustain;
    const float releaseVal     = bank_.size() > 0 ? bank_.resolved(P_ENV_RELEASE)  : params_.envRelease;
    const float lfoRateVal     = bank_.size() > 0 ? bank_.resolved(P_LFO_RATE)     : params_.lfoRate;
    const float lfoDepthVal    = bank_.size() > 0 ? bank_.resolved(P_LFO_DEPTH)    : params_.lfoDepth;
    const float lfoShapeVal    = bank_.size() > 0 ? bank_.resolved(P_LFO_SHAPE)    : params_.lfoShape;
    const float dutyCycleVal   = bank_.size() > 0 ? bank_.resolved(P_DUTY)         : params_.dutyCycle;

    // Determine OscType
    OscType osc = OscType::SawY2K;
    if      (oscTypeVal < 25.f) osc = OscType::SawY2K;
    else if (oscTypeVal < 50.f) osc = OscType::SquarePWM;
    else if (oscTypeVal < 75.f) osc = OscType::SineGlass;
    else                         osc = OscType::NoisePink;

    // Filter (log mapping: 0→20Hz, 100→20kHz)
    const float cutoffHz  = mapLog(cutoffVal / 100.f, 20.f, 20000.f);
    const float res       = resVal / 100.f * 4.f;

    // Envelope (log mapping: 0→1ms, 100→10s)
    const float atkSec    = mapLog(attackVal  / 100.f, 0.001f, 10.f);
    const float decSec    = mapLog(decayVal   / 100.f, 0.001f, 10.f);
    const float sus       = sustainVal / 100.f;
    const float relSec    = mapLog(releaseVal / 100.f, 0.001f, 10.f);

    // LFO (log: 0→0.1Hz, 100→100Hz)
    const float lfoRate   = mapLog(lfoRateVal  / 100.f, 0.1f, 100.f);
    const float lfoDepth  = lfoDepthVal / 100.f;

    LfoShape lfoShape = LfoShape::Sine;
    if      (lfoShapeVal < 33.f)  lfoShape = LfoShape::Sine;
    else if (lfoShapeVal < 66.f)  lfoShape = LfoShape::Triangle;
    else if (lfoShapeVal < 99.f)  lfoShape = LfoShape::Square;
    else                           lfoShape = LfoShape::SampleHold;

    // Volume
    smoothVolume_.setTarget(volumeVal / 100.f);
    smoothVolume_.trigger();

    // Push to all voices
    for (auto& v : voices_) {
        v.oscType      = osc;
        v.dutyCycle    = dutyCycleVal / 100.f;
        v.filterCutoff = cutoffHz;
        v.filterRes    = res;
        v.lfoRate      = lfoRate;
        v.lfoDepth     = lfoDepth;

        // Envelope
        v.env().attackSec    = atkSec;
        v.env().decaySec     = decSec;
        v.env().sustainLevel = sus;
        v.env().releaseSec   = relSec;
    }
}

// ─────────────────────────────────────────────────────────────────
int BondiSynth::getActiveVoiceCount() const noexcept {
    int count = 0;
    for (const auto& v : voices_) if (!v.isIdle()) ++count;
    return count;
}

int BondiSynth::getVoiceNote(int idx) const noexcept {
    if (idx < 0 || idx >= kBondiMaxVoices) return -1;
    return voices_[idx].isIdle() ? -1 : voices_[idx].note();
}

// ─────────────────────────────────────────────────────────────────
//  Parameter access
// ─────────────────────────────────────────────────────────────────
int BondiSynth::getNumParameters() const { return P_NUM_PARAMS; }

float BondiSynth::getParameter(int idx) const {
    switch (idx) {
    case P_OSC_TYPE:      return params_.oscType;
    case P_FILTER_CUTOFF: return params_.filterCutoff;
    case P_FILTER_RES:    return params_.filterRes;
    case P_ENV_ATTACK:    return params_.envAttack;
    case P_ENV_DECAY:     return params_.envDecay;
    case P_ENV_SUSTAIN:   return params_.envSustain;
    case P_ENV_RELEASE:   return params_.envRelease;
    case P_LFO_RATE:      return params_.lfoRate;
    case P_LFO_DEPTH:     return params_.lfoDepth;
    case P_LFO_SHAPE:     return params_.lfoShape;
    case P_PITCH:         return params_.pitchSemitones;
    case P_DUTY:          return params_.dutyCycle;
    case P_PORTAMENTO:    return params_.portamentoTime;
    case P_VOLUME:        return params_.volume;
    default:              return 0.f;
    }
}

void BondiSynth::setParameter(int idx, float value) {
    switch (idx) {
    case P_OSC_TYPE:      params_.oscType          = value; break;
    case P_FILTER_CUTOFF: params_.filterCutoff     = value; break;
    case P_FILTER_RES:    params_.filterRes         = value; break;
    case P_ENV_ATTACK:    params_.envAttack         = value; break;
    case P_ENV_DECAY:     params_.envDecay          = value; break;
    case P_ENV_SUSTAIN:   params_.envSustain        = value; break;
    case P_ENV_RELEASE:   params_.envRelease        = value; break;
    case P_LFO_RATE:      params_.lfoRate           = value; break;
    case P_LFO_DEPTH:     params_.lfoDepth          = value; break;
    case P_LFO_SHAPE:     params_.lfoShape          = value; break;
    case P_PITCH:         params_.pitchSemitones    = value; break;
    case P_DUTY:          params_.dutyCycle         = value; break;
    case P_PORTAMENTO:    params_.portamentoTime    = value; break;
    case P_VOLUME:        params_.volume            = value; break;
    default: break;
    }
    // Sync to ParameterBank base layer so automation/MIDI layers can add on top
    if (idx >= 0 && idx < P_NUM_PARAMS)
        bank_.setBase(idx, value);
    applyParameters();
}

juce::String BondiSynth::getParameterName(int idx) const {
    static const char* names[] = {
        "Osc Type", "Filter Cutoff", "Filter Res",
        "Attack", "Decay", "Sustain", "Release",
        "LFO Rate", "LFO Depth", "LFO Shape",
        "Pitch", "Duty Cycle", "Portamento", "Volume"
    };
    if (idx >= 0 && idx < P_NUM_PARAMS) return names[idx];
    return "Unknown";
}

} // namespace y2k::dsp
