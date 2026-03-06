#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/midi_learn.h
//
//  MidiLearnSystem — bind external MIDI CC to any parameter.
//
//  Workflow:
//    1.  UI calls armLearn(targetType, targetId, paramId)
//    2.  Next incoming MIDI CC → bindMapping() is called automatically
//    3.  Mapping is stored + persisted in session ValueTree
//    4.  processMidi() is called each audio block; dispatches CC values
//        to bound parameters via atomic setters or pushParameterChange()
//
//  Threading:
//    armLearn / getMappings     — UI thread
//    processMidi                — audio thread (RT-safe: no alloc, try_lock)
//    saveToValueTree / load     — any non-RT thread
// ═══════════════════════════════════════════════════════════════════

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  Target types for MIDI Learn
// ─────────────────────────────────────────────────────────────────
enum class MidiLearnTargetType : uint8_t {
    TrackVolume  = 0,
    TrackPan     = 1,
    GraphParam   = 2,   // DSP graph node param (nodeId + paramId)
    AutoParam    = 3,   // Automation lane value
};

// ─────────────────────────────────────────────────────────────────
//  A single CC → parameter binding
// ─────────────────────────────────────────────────────────────────
struct MidiLearnMapping {
    MidiLearnTargetType targetType = MidiLearnTargetType::TrackVolume;
    int                 targetId   = 0;   // trackIndex OR nodeId
    int                 paramId    = 0;   // param index within target
    int                 ccNumber   = -1;  // -1 = unbound
    int                 channel    = 0;   // 0 = any channel
    juce::String        label;            // human-readable name for UI

    bool isBound() const noexcept { return ccNumber >= 0; }

    juce::ValueTree toValueTree() const;
    static MidiLearnMapping fromValueTree(const juce::ValueTree&);
};

// ─────────────────────────────────────────────────────────────────
//  Dispatch callback — called on the audio thread when a bound CC
//  arrives. The callback must be RT-safe (no alloc, no lock wait).
//  Registered by AudioEngine; provides the actual parameter update.
// ─────────────────────────────────────────────────────────────────
using MidiLearnDispatch = std::function<void(const MidiLearnMapping&, float value01)>;

// ─────────────────────────────────────────────────────────────────
//  MidiLearnSystem
// ─────────────────────────────────────────────────────────────────
class MidiLearnSystem {
public:
    MidiLearnSystem() = default;

    // ── UI thread ─────────────────────────────────────────────────

    // Begin waiting for the next CC to arrive and bind it to this target.
    // Pass label="" to auto-generate.
    void armLearn(MidiLearnTargetType type, int targetId, int paramId,
                  const juce::String& label = {});

    // Cancel armed learn without binding
    void cancelLearn() noexcept;

    bool isArmed() const noexcept { return armed_.load(std::memory_order_acquire); }

    // Add/remove mappings manually
    void addMapping(const MidiLearnMapping& m);
    bool removeMapping(int ccNumber, int channel = 0);
    void clearAllMappings();

    const std::vector<MidiLearnMapping>& getMappings() const noexcept { return mappings_; }

    // ── Audio thread ──────────────────────────────────────────────

    // Call this each audio block. Checks each MIDI event; if CC and
    // armed → binds; if CC and mapped → dispatches.
    // RT contract: try_lock on mutex (skips if held), no alloc.
    void processMidi(const juce::MidiBuffer& midi,
                     const MidiLearnDispatch& dispatch) noexcept;

    // ── Persistence ───────────────────────────────────────────────
    juce::ValueTree saveToValueTree() const;
    void            loadFromValueTree(const juce::ValueTree&);

private:
    std::vector<MidiLearnMapping> mappings_;

    // Armed state (written UI, read audio thread)
    std::atomic<bool> armed_ {false};
    MidiLearnMapping  armedTarget_;   // protected by mutex_

    mutable std::mutex mutex_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiLearnSystem)
};

} // namespace y2k::engine
