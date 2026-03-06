#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/param/parameter_state.h
//
//  ParameterState — unified parameter value authority.
//
//  Every controllable parameter in the DSP graph has exactly ONE
//  ParameterState. Four sources can contribute:
//
//    base        = the user's "set and forget" value (knob position)
//    automation  = time-varying offset from automation lane
//    modulation  = LFO / envelope routing (future)
//    midi        = real-time MIDI CC override
//
//  resolved() = clamp(base + automation + modulation + midi_delta, 0, 1)
//
//  Rules:
//    • automation and midi NEVER overwrite base.
//    • resolved() is what the DSP reads — only this function.
//    • base is written from UI thread via atomic store.
//    • automation/modulation written from audio thread — plain floats,
//      no atomic needed (single writer, single reader on audio thread).
//    • midi written from audio thread same way.
//
//  ParameterBank holds all states for one graph node.
//  It is heap-allocated once per node at prepare() time,
//  never during audio callback.
//
//  Usage in DSP node:
//    float cutoff = bank.resolved(PARAM_CUTOFF);
//    // ... use cutoff in filter
// ═══════════════════════════════════════════════════════════════════

#include <atomic>
#include <algorithm>
#include <vector>
#include <cstdint>

namespace y2k::dsp {

// ─────────────────────────────────────────────────────────────────
//  ParameterState  —  one per parameter
// ─────────────────────────────────────────────────────────────────
struct ParameterState {
    // ── Sources ────────────────────────────────────────────────
    std::atomic<float> base        {0.5f};  // UI thread writes
    float              automation  {0.f};   // audio thread writes
    float              modulation  {0.f};   // audio thread writes
    float              midi        {0.f};   // audio thread writes
                                            //   (0 = no override, used as additive delta)
    // ── Limits ─────────────────────────────────────────────────
    float minValue {0.f};
    float maxValue {1.f};

    // ── Resolve  [RT — called per block, no atomic on hot path] ─
    //
    //  Final value = clamp(base + automation + modulation + midi, min, max)
    //
    //  base uses memory_order_relaxed: the audio thread only needs
    //  the value to eventually converge, not on a precise sample boundary.
    //  The ramp in SmoothParameter handles the actual sample-accurate
    //  transition — ParameterState just provides the target.
    float resolved() const noexcept {
        const float b = base.load(std::memory_order_relaxed);
        const float v = b + automation + modulation + midi;
        return std::clamp(v, minValue, maxValue);
    }

    // ── Setters (thread-labelled for clarity) ────────────────
    void setBase      (float v) noexcept { base.store(std::clamp(v, minValue, maxValue), std::memory_order_release); }
    void setAutomation(float v) noexcept { automation = v; }  // audio thread only
    void setModulation(float v) noexcept { modulation = v; }  // audio thread only
    void setMidi      (float v) noexcept { midi       = v; }  // audio thread only

    float getBase() const noexcept { return base.load(std::memory_order_acquire); }

    // ── Copyable value snapshot (for persistence / undo) ─────
    struct Snapshot {
        float base, minValue, maxValue;
    };
    Snapshot snapshot() const noexcept {
        return { base.load(std::memory_order_acquire), minValue, maxValue };
    }
    void restore(const Snapshot& s) noexcept {
        minValue = s.minValue;
        maxValue = s.maxValue;
        base.store(std::clamp(s.base, s.minValue, s.maxValue),
                   std::memory_order_release);
    }
};

// ─────────────────────────────────────────────────────────────────
//  ParameterBank  —  all parameters for one graph node
//
//  Pre-allocated at prepare() time. Never resized during audio.
//  Indexed by integer param ID (0 .. size-1).
// ─────────────────────────────────────────────────────────────────
class ParameterBank {
public:
    ParameterBank() = default;

    // Non-RT: allocate slots. Call once at prepare().
    void initialise(int numParams) {
        params_.clear();
        params_.resize(static_cast<size_t>(numParams));
    }

    int size() const noexcept { return static_cast<int>(params_.size()); }
    bool empty() const noexcept { return params_.empty(); }

    // ── RT: read resolved value ────────────────────────────────
    float resolved(int paramId) const noexcept {
        if (paramId < 0 || paramId >= size()) return 0.f;
        return params_[static_cast<size_t>(paramId)].resolved();
    }

    // ── UI thread: write base ──────────────────────────────────
    void setBase(int paramId, float v) noexcept {
        if (paramId < 0 || paramId >= size()) return;
        params_[static_cast<size_t>(paramId)].setBase(v);
    }

    float getBase(int paramId) const noexcept {
        if (paramId < 0 || paramId >= size()) return 0.f;
        return params_[static_cast<size_t>(paramId)].getBase();
    }

    // ── Audio thread: write automation lane value ─────────────
    void setAutomation(int paramId, float v) noexcept {
        if (paramId < 0 || paramId >= size()) return;
        params_[static_cast<size_t>(paramId)].setAutomation(v);
    }

    // ── Audio thread: write MIDI CC value ────────────────────
    void setMidi(int paramId, float v) noexcept {
        if (paramId < 0 || paramId >= size()) return;
        params_[static_cast<size_t>(paramId)].setMidi(v);
    }

    // ── Range configuration (non-RT) ─────────────────────────
    void setRange(int paramId, float minVal, float maxVal) noexcept {
        if (paramId < 0 || paramId >= size()) return;
        params_[static_cast<size_t>(paramId)].minValue = minVal;
        params_[static_cast<size_t>(paramId)].maxValue = maxVal;
    }

    // Direct access for node internals
    ParameterState& operator[](int i) noexcept {
        return params_[static_cast<size_t>(i)];
    }
    const ParameterState& operator[](int i) const noexcept {
        return params_[static_cast<size_t>(i)];
    }

private:
    std::vector<ParameterState> params_;
};

// ─────────────────────────────────────────────────────────────────
//  ParameterEvent  —  message sent via LockFreeQueue to audio thread
//
//  targetLayer selects which ParameterState field to update:
//    0 = base (from UI knob)
//    1 = automation (from AutomationPlayer)
//    2 = modulation (future LFO)
//    3 = midi (from MidiLearn dispatch)
// ─────────────────────────────────────────────────────────────────
struct ParameterEvent {
    uint32_t nodeId   = 0;
    int      paramId  = 0;
    float    value    = 0.f;
    uint8_t  layer    = 0;   // 0=base 1=auto 2=mod 3=midi
};

} // namespace y2k::dsp
