// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/recording/midi_recorder.cpp
// ═══════════════════════════════════════════════════════════════════

#include "midi_recorder.h"
#include <algorithm>
#include <cmath>

namespace y2k::dsp {

MidiRecorder::MidiRecorder(int maxEvents) : maxEvents_(maxEvents) {
    events_.resize(maxEvents_);
}

void MidiRecorder::arm(double sampleRate) {
    sampleRate_ = sampleRate;
    eventCount_.store(0, std::memory_order_relaxed);
    armed_ = true;
}

void MidiRecorder::disarm() {
    recording_.store(false, std::memory_order_release);
    armed_ = false;
}

void MidiRecorder::startCapture(double startBeat, double bpm) {
    if (!armed_) return;
    startBeat_ = startBeat;
    bpm_       = bpm;
    eventCount_.store(0, std::memory_order_relaxed);
    recording_.store(true, std::memory_order_release);
}

void MidiRecorder::stopCapture() {
    recording_.store(false, std::memory_order_release);
}

// ─────────────────────────────────────────────────────────────────
//  processBlock  [RT]
// ─────────────────────────────────────────────────────────────────
void MidiRecorder::processBlock(const juce::MidiBuffer& midi,
                                double                  ppqStart,
                                double                  bpm,
                                int                     numSamples,
                                double                  sampleRate) noexcept
{
    if (!recording_.load(std::memory_order_acquire)) return;
    if (numSamples <= 0 || sampleRate <= 0.0) return;

    const double beatsPerSample = bpm / (60.0 * sampleRate);
    int count = eventCount_.load(std::memory_order_relaxed);

    for (const auto meta : midi) {
        if (count >= maxEvents_) break;
        const auto msg = meta.getMessage();
        if (!msg.isNoteOnOrOff()) continue;

        const double eventBeat = ppqStart + meta.samplePosition * beatsPerSample;

        RawEvent& ev = events_[count++];
        ev.beat     = eventBeat;
        ev.note     = msg.getNoteNumber();
        ev.velocity = msg.isNoteOn() ? msg.getFloatVelocity() : 0.f;
        ev.channel  = msg.getChannel();
    }

    eventCount_.store(count, std::memory_order_release);
}

// ─────────────────────────────────────────────────────────────────
//  buildClip  [non-RT]
// ─────────────────────────────────────────────────────────────────
std::unique_ptr<y2k::engine::MidiClip> MidiRecorder::buildClip(
    double clipStartBeat,
    const juce::String& name) const
{
    const int count = eventCount_.load(std::memory_order_acquire);
    auto clip = std::make_unique<y2k::engine::MidiClip>();
    clip->name       = name;
    clip->startBeat  = clipStartBeat;

    // Match note-ons with note-offs
    struct OpenNote { double beatOn; int note; float vel; int ch; };
    std::vector<OpenNote> open;

    double maxBeat = clipStartBeat;

    for (int i = 0; i < count; ++i) {
        const RawEvent& ev = events_[i];
        const double localBeat = ev.beat - clipStartBeat;

        if (ev.velocity > 0.f) {
            open.push_back({ ev.beat, ev.note, ev.velocity, ev.channel });
        } else {
            // Find matching open note
            for (auto it = open.begin(); it != open.end(); ++it) {
                if (it->note == ev.note && it->ch == ev.channel) {
                    y2k::engine::MidiClip::Note n;
                    n.pitch         = it->note;
                    n.velocity      = it->vel;
                    n.startBeat     = it->beatOn - clipStartBeat;
                    n.durationBeats = ev.beat - it->beatOn;
                    if (n.durationBeats < 0.01) n.durationBeats = 0.125; // 32nd minimum
                    clip->notes.push_back(n);
                    maxBeat = std::max(maxBeat, ev.beat);
                    open.erase(it);
                    break;
                }
            }
        }
    }

    // Close orphaned notes (held when recording stopped) — 1-beat fallback
    for (const auto& o : open) {
        y2k::engine::MidiClip::Note n;
        n.pitch         = o.note;
        n.velocity      = o.vel;
        n.startBeat     = o.beatOn - clipStartBeat;
        n.durationBeats = 1.0;
        clip->notes.push_back(n);
        maxBeat = std::max(maxBeat, o.beatOn + 1.0);
    }

    // Clip duration = next bar boundary after last note
    const double rawDur = maxBeat - clipStartBeat;
    clip->durationBeats = std::ceil(rawDur / 4.0) * 4.0;
    if (clip->durationBeats < 4.0) clip->durationBeats = 4.0;

    // Sort notes by start beat
    std::sort(clip->notes.begin(), clip->notes.end(),
        [](const auto& a, const auto& b){ return a.startBeat < b.startBeat; });

    return clip;
}

} // namespace y2k::dsp
