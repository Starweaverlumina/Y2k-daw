#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/mixer/clip_scheduler.h  — v8 (fully fleshed)
//
//  ClipScheduler — reads MidiClip notes from Y2KSession and fires
//  juce::MidiMessage events into the audio callback's MidiBuffer at
//  the correct sample offset within each block.
//
//  Threading model (v8):
//    updateFromSession()  — non-RT: builds NoteEvent list, atomic swap
//    fillMidiBuffer()     — RT: atomic load of snapshot, no mutex
//
//  v8 fixes vs v7:
//    ✓ Removed std::mutex from RT path (was try_lock — not guaranteed lock-free)
//    ✓ Uses atomic<shared_ptr<EventList>> for zero-lock double-buffer
//    ✓ fillMidiBuffer() now takes bpm — computes exact ppqEnd per block
//    ✓ Sample-accurate offset using bpm/sampleRate
//    ✓ No more 0.5-beat fixed lookahead kludge
// ═══════════════════════════════════════════════════════════════════

#include "../graph/processor_graph.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <vector>

namespace y2k::engine { class Y2KSession; }

namespace y2k::dsp {

class ClipScheduler {
public:
    ClipScheduler() = default;

    // Non-RT: rebuild event list and atomically publish to RT thread.
    void updateFromSession(const y2k::engine::Y2KSession& session,
                           double bpm,
                           int    midiChannel = 1);

    // RT: emit MIDI events for this block.
    //   ppqStart   — beat position at first sample of block
    //   numSamples — block size
    //   sampleRate — device sample rate (for computing sample offsets)
    //   bpm        — current tempo (for computing ppqEnd exactly)
    //   isPlaying  — silence output when stopped
    void fillMidiBuffer(juce::MidiBuffer& outMidi,
                        double            ppqStart,
                        int               numSamples,
                        double            sampleRate,
                        double            bpm,
                        bool              isPlaying) noexcept;

    // Non-RT: discard all events.
    void clear() noexcept;

    int getEventCount() const noexcept;

private:
    struct NoteEvent {
        double beat;
        int    note;
        float  velocity;
        bool   isNoteOn;
        int    midiChannel;
    };

    using EventList = std::vector<NoteEvent>;

    // Immutable snapshot shared between non-RT builder and RT consumer.
    // Atomic shared_ptr swap — same pattern as GraphState.
    std::atomic<std::shared_ptr<const EventList>> activeList_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipScheduler)
};

} // namespace y2k::dsp
