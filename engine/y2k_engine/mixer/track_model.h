#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/mixer/track_model.h  — v9
//
//  TrackModel and BusModel — musical intent data layer.
//
//  Intentionally separate from ProcessGraph execution.
//  TrackModel edits happen on UI thread.
//  GraphCompiler translates TrackModel → ProcessGraph (non-RT).
//
//  ──────────────────────────────────────────────────────────────
//  Design rule (from blueprint):
//    TrackModel edits → GraphCompiler → new ProcessGraph → commitState()
//    NEVER mutate ProcessGraph directly from a TrackModel field change.
//  ──────────────────────────────────────────────────────────────
//
//  InsertSlot: one processor in the insert chain.
//    typeId = string identifier ("SineOsc", "BondiSynth", "GrapeComp", etc.)
//    params = serialized base parameter values
//
//  SendSlot: one send from this track to a bus.
//    targetBusId  = BusModel::id of the target bus
//    level        = 0..1 linear send level
//    preFader     = true → tap before fader; false → post-fader
//    enabled      = false → send silenced (but slot retained)
//
//  TrackInput: what feeds this track
//    None         → silent (automation/test only)
//    Instrument   → instrument processor (SineOsc, BondiSynth, etc.)
//    AudioIn      → audio device input channels
//    BusReceive   → receive from bus (for sub-group tracks)
//
//  MeterTapPoint: where in chain to tap for metering
//    PostInsert   → after last insert, before fader
//    PostFader    → after fader (default)
// ═══════════════════════════════════════════════════════════════════

#include <juce_core/juce_core.h>
#include <vector>
#include <optional>
#include <string>

namespace y2k::engine {

// ─────────────────────────────────────────────────────────────────
//  InsertSlot — one processor in the insert chain
// ─────────────────────────────────────────────────────────────────
struct InsertSlot {
    juce::String              typeId;    // "SineOsc" | "BondiSynth" | "GrapeComp" | ...
    bool                      bypassed = false;
    std::vector<std::pair<int,float>> params; // paramIdx → base value
};

// ─────────────────────────────────────────────────────────────────
//  SendSlot — one send from track → bus
// ─────────────────────────────────────────────────────────────────
struct SendSlot {
    juce::Uuid   targetBusId;   // BusModel::id
    float        level      = 1.f;
    bool         preFader   = false;
    bool         enabled    = true;
};

// ─────────────────────────────────────────────────────────────────
//  Enums
// ─────────────────────────────────────────────────────────────────
enum class TrackInputType : uint8_t {
    None,
    Instrument,   // Has an instrument processor as first node
    AudioIn,      // Receives from audio device input
    BusReceive    // Sub-group: receives from a bus
};

enum class MeterTapPoint : uint8_t {
    PostInsert,   // After insert chain, before fader
    PostFader     // After fader (default, reflects what you hear)
};

// ─────────────────────────────────────────────────────────────────
//  TrackModel — complete track routing descriptor
// ─────────────────────────────────────────────────────────────────
struct TrackModel {
    juce::Uuid    id;
    juce::String  name;

    // ── Signal path ───────────────────────────────────────────────
    TrackInputType  inputType    = TrackInputType::Instrument;
    juce::String    instrumentId = "SineOsc"; // if inputType == Instrument
    int             audioInCh[2] = {0, 1};    // if inputType == AudioIn

    // Insert chain: processed in order 0..N-1
    std::vector<InsertSlot> inserts;

    // Sends: routed to bus before/after fader per preFader flag
    std::vector<SendSlot>   sends;

    // ── Fader ─────────────────────────────────────────────────────
    float volume = 0.8f;   // 0..1 linear
    float pan    = 0.f;    // -1..+1
    bool  muted  = false;
    bool  soloed = false;

    // ── Routing target ────────────────────────────────────────────
    // If empty, track routes to master bus.
    // If set, track routes to the specified bus (sub-group).
    std::optional<juce::Uuid> outputBusId;

    // ── Metering ──────────────────────────────────────────────────
    MeterTapPoint meterTap = MeterTapPoint::PostFader;

    // ── Compiled node IDs (set by GraphCompiler, read-only) ───────
    // These exist so UI can look up meter data, param automation, etc.
    // DO NOT use these to mutate the graph directly.
    struct CompiledIds {
        uint32_t instrumentNodeId  = 0;
        uint32_t insertChainLastId = 0;  // last insert node (for PDC calc)
        uint32_t faderNodeId       = 0;
        uint32_t meterTapNodeId    = 0;
        std::vector<uint32_t> sendTapNodeIds; // one per send slot
    };
    CompiledIds compiled;
};

// ─────────────────────────────────────────────────────────────────
//  BusModel — bus/group/aux routing descriptor
// ─────────────────────────────────────────────────────────────────
struct BusModel {
    juce::Uuid    id;
    juce::String  name;
    bool          isMaster = false;

    // Bus FX chain (insert chain on the bus)
    std::vector<InsertSlot> inserts;

    // Bus output volume
    float volume = 1.f;
    bool  muted  = false;

    MeterTapPoint meterTap = MeterTapPoint::PostFader;

    struct CompiledIds {
        uint32_t sumNodeId      = 0;   // BusSumNode
        uint32_t insertLastId   = 0;   // last bus insert node
        uint32_t faderNodeId    = 0;   // bus fader
        uint32_t meterTapNodeId = 0;
    };
    CompiledIds compiled;
};

// ─────────────────────────────────────────────────────────────────
//  ProjectRouting — top-level routing document
//
//  This is the complete description of the mixing topology.
//  GraphCompiler::compile(ProjectRouting) → ProcessGraph.
// ─────────────────────────────────────────────────────────────────
struct ProjectRouting {
    std::vector<TrackModel> tracks;
    std::vector<BusModel>   buses;
    BusModel                masterBus;   // always present; isMaster=true

    // Factory: create a basic 4-track + master routing
    static ProjectRouting createDefault();

    // Validate: check for missing bus references, duplicate IDs, etc.
    juce::Result validate() const;

    // Find bus by ID
    const BusModel* findBus(const juce::Uuid& id) const noexcept;
    BusModel*       findBus(const juce::Uuid& id) noexcept;
};

} // namespace y2k::engine
