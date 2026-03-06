#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_engine/graph_synth/graph_compiler.h  — v9
//
//  GraphCompiler — translates ProjectRouting → ProcessGraph.
//
//  This is the "compilation step" that converts musical intent
//  (tracks, buses, sends) into a concrete DSP graph.
//
//  ── Output graph topology for each track ─────────────────────
//
//  Instrument track, post-fader send to bus B, meter post-fader:
//
//    [InstrumentNode]
//         │
//    [Insert₀] → [Insert₁] → ... → [InsertN]   (if any)
//         │
//    [PreFaderSendTap]   (if preFader sends exist)
//         │ ch 0+1=main, ch 2+3=send
//         │                     ↘ to BusSumNode(B)
//    [TrackFaderNode]
//         │
//    [PostFaderSendTap]  (if postFader sends exist)
//         │ ch 0+1=main, ch 2+3=send
//         │                     ↘ to BusSumNode(B2)
//    [MeterTapNode]
//         │
//         ↓ to MasterSumNode (or BusSumNode if outputBus set)
//
//  Bus topology:
//
//    [BusSumNode]   ← receives from tracks and sends
//    [BusInsert₀] → [BusInsertN]
//    [BusFaderNode]
//    [BusMeterTap]
//         ↓ to MasterSumNode
//
//  Master:
//
//    [MasterSumNode] ← receives from tracks and buses
//    [MasterInsert₀] → [MasterInsertN]
//    [MasterFader]
//    [MasterMeter]
//         ↓ to OUTPUT_NODE_ID
//
//  ── PDC integration ──────────────────────────────────────────
//
//  SumNodes (BusSumNode, MasterSumNode) are where paths converge.
//  PDCEngine::computeAndInject() sees these as convergence points
//  and inserts LatencyCompensatorNodes on shorter paths before swap.
//
//  ── Thread model ─────────────────────────────────────────────
//
//  GraphCompiler::compile() is non-RT. Call from UI thread.
//  After compile(), call graph.prepare() then graph.commitState().
//  The audio thread only sees the result via atomic swap.
// ═══════════════════════════════════════════════════════════════════

#include "../../y2k_dsp/graph/processor_graph.h"
#include "../../y2k_dsp/mixer/sum_node.h"
#include "../../y2k_dsp/mixer/track_fader_node.h"
#include "../../y2k_dsp/mixer/send_tap_node.h"
#include "../../y2k_dsp/meter/meter_tap.h"
#include "../../y2k_dsp/generators/sine_osc.h"
#include "../../y2k_dsp/instruments/bondi_synth.h"
#include "../mixer/track_model.h"

namespace y2k::engine {

class GraphCompiler {
public:
    // ── Result: compiled graph + node ID maps ────────────────────
    struct Result {
        // The compiled graph (caller calls prepare() + commitState())
        y2k::dsp::ProcessGraph graph;

        // Meter tap nodes, indexed by track/bus name → MeterTap*
        // These stay alive as long as graph.nodes_ holds them.
        // UI thread reads meters via these pointers (atomic reads).
        std::vector<std::pair<juce::String, y2k::dsp::MeterTap*>> trackMeters;
        std::vector<std::pair<juce::String, y2k::dsp::MeterTap*>> busMeters;
        y2k::dsp::MeterTap*                                        masterMeter = nullptr;

        // Graph revision at compile time (for cache invalidation)
        uint64_t compiledRevision = 0;
    };

    // ── compile ──────────────────────────────────────────────────
    //  Main entry. Validates routing, builds graph, returns Result.
    //  Caller should call result.graph.prepare(spec) after.
    //  PDC will run inside the subsequent commitState().
    static Result compile(ProjectRouting& routing) {
        Result result;

        // Validate first
        auto vr = routing.validate();
        if (!vr.wasOk()) {
            juce::Logger::writeToLog("[GraphCompiler] Validation failed: " + vr.getErrorMessage());
            return result;
        }

        auto& graph = result.graph;

        // ── Step 1: Create bus sum nodes ───────────────────────────
        // We need bus sum nodes before tracks, so tracks can wire sends to them.

        // Master sum node (all tracks + buses converge here)
        auto masterSum = std::make_unique<y2k::dsp::SumNode>(2,2,1.f);
        masterSum->name = "MasterSum";
        auto* masterSumRaw = graph.addNode(std::move(masterSum));
        routing.masterBus.compiled.sumNodeId = masterSumRaw ? masterSumRaw->nodeId : 0;

        // Non-master bus sum nodes
        for (auto& bus : routing.buses) {
            auto busSum = std::make_unique<y2k::dsp::SumNode>(2,2,1.f);
            busSum->name = bus.name + " Sum";
            auto* raw = graph.addNode(std::move(busSum));
            bus.compiled.sumNodeId = raw ? raw->nodeId : 0;
        }

        // ── Step 2: Build each track chain ────────────────────────
        for (auto& t : routing.tracks) {
            compileTrack(t, routing, graph, result);
        }

        // ── Step 3: Build each non-master bus chain ────────────────
        for (auto& bus : routing.buses) {
            compileBus(bus, routing.masterBus, graph, result.busMeters);
        }

        // ── Step 4: Build master bus chain ────────────────────────
        compileMasterBus(routing.masterBus, graph, result);

        // Wire master sum → master chain head (done inside compileMasterBus)
        // Wire final master node → OUTPUT
        // (compileMasterBus handles this)

        return result;
    }

private:
    // ── compileTrack ──────────────────────────────────────────────
    static void compileTrack(TrackModel& t, ProjectRouting& routing,
                              y2k::dsp::ProcessGraph& graph,
                              Result& result)
    {
        uint32_t currentTailId = 0;
        int      currentCh     = 0; // output channel index on tailNode

        // ── Instrument / input source ──────────────────────────────
        if (t.inputType == TrackInputType::Instrument) {
            auto* instrNode = createInstrumentNode(t.instrumentId, graph);
            if (instrNode) {
                t.compiled.instrumentNodeId = instrNode->nodeId;
                currentTailId = instrNode->nodeId;
                currentCh     = 0;
            }
        } else if (t.inputType == TrackInputType::AudioIn) {
            // Wire device input directly (use INPUT_NODE_ID as source)
            currentTailId = y2k::dsp::ProcessGraph::INPUT_NODE_ID;
            currentCh     = t.audioInCh[0];
        }

        if (currentTailId == 0) return; // no source

        // ── Insert chain ───────────────────────────────────────────
        for (int i = 0; i < int(t.inserts.size()); ++i) {
            auto& slot = t.inserts[i];
            // For v9 we support a limited insert set; plugins in v10
            auto* insertNode = createInsertNode(slot, graph);
            if (!insertNode) continue;
            // Wire tail → insert
            wireStereo(graph, currentTailId, insertNode->nodeId);
            currentTailId = insertNode->nodeId;
            if (i == int(t.inserts.size())-1)
                t.compiled.insertChainLastId = insertNode->nodeId;
        }

        // ── Pre-fader sends ───────────────────────────────────────
        for (int si = 0; si < int(t.sends.size()); ++si) {
            auto& send = t.sends[si];
            if (!send.preFader || !send.enabled) continue;

            const auto* targetBus = routing.findBus(send.targetBusId);
            if (!targetBus) continue;

            auto tap = std::make_unique<y2k::dsp::SendTapNode>(
                targetBus->compiled.sumNodeId, send.level);
            tap->name = t.name + " PreSend→" + targetBus->name;
            auto* tapRaw = graph.addNode(std::move(tap));
            if (!tapRaw) continue;

            wireStereo(graph, currentTailId, tapRaw->nodeId);
            // Send output (ch 2+3) → bus sum
            graph.connect({ tapRaw->nodeId, 2, targetBus->compiled.sumNodeId, 0 });
            graph.connect({ tapRaw->nodeId, 3, targetBus->compiled.sumNodeId, 1 });
            // Main path continues from tap passthrough (ch 0+1)
            currentTailId = tapRaw->nodeId;
            t.compiled.sendTapNodeIds.push_back(tapRaw->nodeId);
        }

        // ── Fader ─────────────────────────────────────────────────
        auto fader = std::make_unique<y2k::dsp::TrackFaderNode>(t.volume, t.pan);
        fader->name = t.name + " Fader";
        fader->setMuted(t.muted);
        fader->setSoloed(t.soloed);
        auto* faderRaw = graph.addNode(std::move(fader));
        if (faderRaw) {
            wireStereo(graph, currentTailId, faderRaw->nodeId);
            t.compiled.faderNodeId = faderRaw->nodeId;
            currentTailId = faderRaw->nodeId;
        }

        // ── Post-fader sends ──────────────────────────────────────
        for (auto& send : t.sends) {
            if (send.preFader || !send.enabled) continue;
            const auto* targetBus = routing.findBus(send.targetBusId);
            if (!targetBus) continue;

            auto tap = std::make_unique<y2k::dsp::SendTapNode>(
                targetBus->compiled.sumNodeId, send.level);
            tap->name = t.name + " PostSend→" + targetBus->name;
            auto* tapRaw = graph.addNode(std::move(tap));
            if (!tapRaw) continue;
            wireStereo(graph, currentTailId, tapRaw->nodeId);
            graph.connect({ tapRaw->nodeId, 2, targetBus->compiled.sumNodeId, 0 });
            graph.connect({ tapRaw->nodeId, 3, targetBus->compiled.sumNodeId, 1 });
            currentTailId = tapRaw->nodeId;
            t.compiled.sendTapNodeIds.push_back(tapRaw->nodeId);
        }

        // ── Meter tap ─────────────────────────────────────────────
        {
            auto meter = std::make_unique<y2k::dsp::MeterTap>(t.name + " Meter");
            auto* meterRaw = graph.addNode(std::move(meter));
            if (meterRaw) {
                wireStereo(graph, currentTailId, meterRaw->nodeId);
                t.compiled.meterTapNodeId = meterRaw->nodeId;
                result.trackMeters.push_back({t.name, static_cast<y2k::dsp::MeterTap*>(
                    graph.getNode(meterRaw->nodeId))});
                currentTailId = meterRaw->nodeId;
            }
        }

        // ── Route to output bus or master sum ─────────────────────
        uint32_t destSumId = routing.masterBus.compiled.sumNodeId;
        if (t.outputBusId.has_value()) {
            if (const auto* bus = routing.findBus(*t.outputBusId))
                destSumId = bus->compiled.sumNodeId;
        }
        if (destSumId != 0) {
            graph.connect({ currentTailId, 0, destSumId, 0 });
            graph.connect({ currentTailId, 1, destSumId, 1 });
        }
    }

    // ── compileBus ────────────────────────────────────────────────
    static void compileBus(BusModel& bus, BusModel& masterBus,
                           y2k::dsp::ProcessGraph& graph,
                           std::vector<std::pair<juce::String, y2k::dsp::MeterTap*>>& busMeters)
    {
        uint32_t currentTailId = bus.compiled.sumNodeId;

        // Bus insert chain
        for (auto& slot : bus.inserts) {
            auto* node = createInsertNode(slot, graph);
            if (!node) continue;
            wireStereo(graph, currentTailId, node->nodeId);
            currentTailId = node->nodeId;
            bus.compiled.insertLastId = node->nodeId;
        }

        // Bus fader
        auto fader = std::make_unique<y2k::dsp::TrackFaderNode>(bus.volume, 0.f);
        fader->name = bus.name + " Fader";
        fader->setMuted(bus.muted);
        auto* faderRaw = graph.addNode(std::move(fader));
        if (faderRaw) {
            wireStereo(graph, currentTailId, faderRaw->nodeId);
            bus.compiled.faderNodeId = faderRaw->nodeId;
            currentTailId = faderRaw->nodeId;
        }

        // Bus meter
        auto meter = std::make_unique<y2k::dsp::MeterTap>(bus.name + " Meter");
        auto* meterRaw = graph.addNode(std::move(meter));
        if (meterRaw) {
            wireStereo(graph, currentTailId, meterRaw->nodeId);
            bus.compiled.meterTapNodeId = meterRaw->nodeId;
            busMeters.push_back({bus.name, static_cast<y2k::dsp::MeterTap*>(
                graph.getNode(meterRaw->nodeId))});
            currentTailId = meterRaw->nodeId;
        }

        // Bus → master sum
        const uint32_t masterSumId = masterBus.compiled.sumNodeId;
        if (masterSumId != 0) {
            graph.connect({ currentTailId, 0, masterSumId, 0 });
            graph.connect({ currentTailId, 1, masterSumId, 1 });
        }
    }

    // ── compileMasterBus ──────────────────────────────────────────
    static void compileMasterBus(BusModel& masterBus,
                                  y2k::dsp::ProcessGraph& graph,
                                  Result& result)
    {
        uint32_t currentTailId = masterBus.compiled.sumNodeId;

        // Master insert chain
        for (auto& slot : masterBus.inserts) {
            auto* node = createInsertNode(slot, graph);
            if (!node) continue;
            wireStereo(graph, currentTailId, node->nodeId);
            currentTailId = node->nodeId;
            masterBus.compiled.insertLastId = node->nodeId;
        }

        // Master fader
        auto fader = std::make_unique<y2k::dsp::TrackFaderNode>(masterBus.volume, 0.f);
        fader->name = "Master Fader";
        auto* faderRaw = graph.addNode(std::move(fader));
        if (faderRaw) {
            wireStereo(graph, currentTailId, faderRaw->nodeId);
            masterBus.compiled.faderNodeId = faderRaw->nodeId;
            currentTailId = faderRaw->nodeId;
        }

        // Master meter
        auto meter = std::make_unique<y2k::dsp::MeterTap>("Master Meter");
        auto* meterRaw = graph.addNode(std::move(meter));
        if (meterRaw) {
            wireStereo(graph, currentTailId, meterRaw->nodeId);
            masterBus.compiled.meterTapNodeId = meterRaw->nodeId;
            result.masterMeter = static_cast<y2k::dsp::MeterTap*>(
                graph.getNode(meterRaw->nodeId));
            currentTailId = meterRaw->nodeId;
        }

        // Master → OUTPUT
        graph.connect({ currentTailId, 0, y2k::dsp::ProcessGraph::OUTPUT_NODE_ID, 0 });
        graph.connect({ currentTailId, 1, y2k::dsp::ProcessGraph::OUTPUT_NODE_ID, 1 });
    }

    // ── Helpers ───────────────────────────────────────────────────
    static void wireStereo(y2k::dsp::ProcessGraph& graph,
                            uint32_t srcId, uint32_t dstId)
    {
        graph.connect({ srcId, 0, dstId, 0 });
        graph.connect({ srcId, 1, dstId, 1 });
    }

    static y2k::dsp::ProcessorNode* createInstrumentNode(
        const juce::String& typeId, y2k::dsp::ProcessGraph& graph)
    {
        if (typeId == "SineOsc" || typeId == "sine_osc") {
            auto n = std::make_unique<y2k::dsp::SineOscProcessor>(440.f, 0.12f);
            return graph.addNode(std::move(n));
        }
        if (typeId == "BondiSynth" || typeId == "bondi") {
            auto n = std::make_unique<y2k::dsp::BondiSynth>();
            return graph.addNode(std::move(n));
        }
        juce::Logger::writeToLog("[GraphCompiler] Unknown instrument: " + typeId);
        return nullptr;
    }

    static y2k::dsp::ProcessorNode* createInsertNode(
        const InsertSlot& slot, y2k::dsp::ProcessGraph& graph)
    {
        if (slot.bypassed) return nullptr;
        // v9: limited built-in inserts; plugins in v10
        if (slot.typeId == "TrackGain") {
            auto n = std::make_unique<y2k::dsp::TrackFaderNode>();
            n->name = "Insert:TrackGain";
            auto* raw = graph.addNode(std::move(n));
            if (raw) for (auto& [idx,val] : slot.params) raw->setParameter(idx, val);
            return raw;
        }
        // Unknown insert type: log + skip
        juce::Logger::writeToLog("[GraphCompiler] Unknown insert type: " + slot.typeId);
        return nullptr;
    }
};

} // namespace y2k::engine
