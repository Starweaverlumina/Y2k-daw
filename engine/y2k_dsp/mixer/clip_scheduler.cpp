// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/mixer/clip_scheduler.cpp  — v8
// ═══════════════════════════════════════════════════════════════════

#include "clip_scheduler.h"
#include "../../y2k_engine/session.h"

#include <algorithm>
#include <cmath>

namespace y2k::dsp {

// ─────────────────────────────────────────────────────────────────
//  updateFromSession  [non-RT]
// ─────────────────────────────────────────────────────────────────
void ClipScheduler::updateFromSession(const y2k::engine::Y2KSession& session,
                                      double /*bpm*/,
                                      int    midiChannel)
{
    auto events = std::make_shared<EventList>();

    for (const auto& track : session.tracks) {
        for (const auto& clip : track->clips) {
            const auto* mc = dynamic_cast<const y2k::engine::MidiClip*>(clip.get());
            if (!mc) continue;

            const double clipStart = mc->startBeat;

            for (const auto& note : mc->notes) {
                const double onBeat  = clipStart + note.startBeat;
                const double offBeat = onBeat + note.durationBeats;
                if (onBeat >= clipStart + mc->durationBeats) continue;
                const double clampedOff = std::min(offBeat, clipStart + mc->durationBeats);

                const int midiNote = std::clamp(note.pitch, 0, 127);
                const int vel      = std::clamp(int(note.velocity * 127.f), 1, 127);

                events->push_back({ onBeat,      midiNote, float(vel)/127.f, true,  midiChannel });
                events->push_back({ clampedOff,  midiNote, 0.f,               false, midiChannel });
            }
        }
    }

    std::sort(events->begin(), events->end(),
        [](const NoteEvent& a, const NoteEvent& b){ return a.beat < b.beat; });

    // Atomic publish — audio thread's existing shared_ptr stays valid
    // until it drops at end of current block.
    activeList_.store(events, std::memory_order_release);
}

void ClipScheduler::clear() noexcept {
    activeList_.store(nullptr, std::memory_order_release);
}

int ClipScheduler::getEventCount() const noexcept {
    auto list = activeList_.load(std::memory_order_acquire);
    return list ? int(list->size()) : 0;
}

// ─────────────────────────────────────────────────────────────────
//  fillMidiBuffer  [RT — no allocation, no lock]
//
//  ppqEnd is computed exactly from bpm + block size:
//    ppqEnd = ppqStart + (bpm / 60.0) * (numSamples / sampleRate)
//
//  Sample offset for each event:
//    sampleOffset = round((beat - ppqStart) / beatsPerSample)
//
//  RT contract:
//    ✓ atomic load of shared_ptr (ref-count bump, no malloc on x86-64/arm64)
//    ✓ No mutex, no try_lock, no blocking
//    ✓ No heap allocation
//    ✓ Linear scan of pre-sorted, pre-allocated EventList
//    ✓ MidiBuffer::addEvent is RT-safe (pre-allocated)
// ─────────────────────────────────────────────────────────────────
void ClipScheduler::fillMidiBuffer(juce::MidiBuffer& outMidi,
                                   double            ppqStart,
                                   int               numSamples,
                                   double            sampleRate,
                                   double            bpm,
                                   bool              isPlaying) noexcept
{
    if (!isPlaying || numSamples <= 0 || sampleRate <= 0.0 || bpm <= 0.0) return;

    auto list = activeList_.load(std::memory_order_acquire);
    if (!list || list->empty()) return;

    // Exact beat window for this block
    const double beatsPerSample = bpm / (60.0 * sampleRate);
    const double ppqEnd         = ppqStart + beatsPerSample * numSamples;

    for (const auto& ev : *list) {
        if (ev.beat < ppqStart) continue;
        if (ev.beat >= ppqEnd)  break;  // sorted — early exit

        // Sample-accurate offset within block
        const double beatOffset  = ev.beat - ppqStart;
        const int    sampleOff   = int(std::clamp(beatOffset / beatsPerSample,
                                                   0.0, double(numSamples - 1)));
        if (ev.isNoteOn) {
            outMidi.addEvent(
                juce::MidiMessage::noteOn(ev.midiChannel, ev.note,
                    uint8_t(int(ev.velocity * 127.f))),
                sampleOff);
        } else {
            outMidi.addEvent(
                juce::MidiMessage::noteOff(ev.midiChannel, ev.note),
                sampleOff);
        }
    }
}

} // namespace y2k::dsp
