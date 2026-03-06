#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/graph/pdc.h  — v9
//
//  PluginDelayCompensation — graph-level latency propagation.
//
//  Called from ProcessGraph::commitState() BEFORE topo sort finalization.
//  Mutates the node list and connection list to inject compensators.
//  After injection, ProcessGraph rebuilds topo order.
//
//  Algorithm: Model A (max-path accumulation)
//
//  Step 1 — Compute cumulative latency (cum[nodeId]):
//    For each node in topo order:
//      cum[node] = max(cum[predecessor] + predecessor.getLatencySamples())
//                  over all predecessors with edges into node.
//    INPUT_NODE_ID has cum = 0.
//
//  Step 2 — Inject compensators at sum nodes:
//    For each node where isSumNode() == true (or multiple inputs):
//      maxCum = max(cum[pred] + pred.getLatencySamples()) for all preds
//      For each pred:
//        delay = maxCum - (cum[pred] + pred.getLatencySamples())
//        if delay > 0:
//          Insert LatencyCompensatorNode(delay) on the edge
//          pred → compensator → sumNode
//
//  Step 3 — Rebuild cum[] (compensators have 0 latency, so cum unchanged
//    except that their own nodeId now exists in the map).
//
//  Preconditions:
//    • Graph is a DAG (cycle detection already done in ProcessGraph)
//    • nodes and connections are the PRE-commit mutable vectors
//    • ProcessSpec is available (for compensator sizing)
//
//  PDC does NOT:
//    • Allocate in the RT path (compensators are created here, non-RT)
//    • Hold any state across blocks (stateless algorithm)
//    • Run if there are no nodes with latency > 0 (early exit)
//
//  Output:
//    Modified nodes vector (compensators appended)
//    Modified connections vector (edges redirected through compensators)
// ═══════════════════════════════════════════════════════════════════

#include "processor_graph.h"
#include "latency_compensator.h"
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <memory>

namespace y2k::dsp {

class PDCEngine {
public:
    // ── computeAndInject ──────────────────────────────────────────
    //
    //  Main entry point — called from ProcessGraph::commitState()
    //  after Kahn sort but before atomic swap.
    //
    //  topoOrder  — nodes in execution order (output of Kahn sort)
    //  nodes      — mutable node list (compensators appended here)
    //  connections — mutable connection list (edges rewired here)
    //  spec       — for sizing compensator ring buffers
    //
    //  Returns the number of compensators injected.
    //  Returns 0 if no latency exists in the graph (early exit).
    static int computeAndInject(
        const std::vector<ProcessorNode*>&           topoOrder,
        std::vector<std::unique_ptr<ProcessorNode>>& nodes,
        std::vector<Connection>&                     connections,
        const ProcessSpec&                           spec,
        uint32_t&                                    nextNodeId)
    {
        // ── Step 0: Compute max node latency across graph ──────────
        int maxLatency = 0;
        for (auto* n : topoOrder) {
            if (n) maxLatency = std::max(maxLatency, n->getLatencySamples());
        }
        // Check all existing compensators too
        for (auto& n : nodes) {
            if (n) maxLatency = std::max(maxLatency, n->getLatencySamples());
        }

        // Early exit: if no node has any latency, nothing to compensate
        if (maxLatency == 0) return 0;

        // ── Step 1: Cumulative latency propagation ─────────────────
        //
        //  cum[id] = max cumulative latency reaching the INPUT of that node
        //  (i.e., the latency produced by ALL predecessors on the longest path)
        std::unordered_map<uint32_t, int> cum;
        cum[ProcessGraph::INPUT_NODE_ID] = 0;

        // Build adjacency: for each node, which nodes feed into it?
        // predecessorLatency[dst][src] = cum[src] + src.getLatencySamples()
        std::unordered_map<uint32_t, int> nodeLat;
        for (auto* n : topoOrder) {
            if (n) nodeLat[n->nodeId] = n->getLatencySamples();
        }
        nodeLat[ProcessGraph::INPUT_NODE_ID]  = 0;
        nodeLat[ProcessGraph::OUTPUT_NODE_ID] = 0;

        // Walk in topo order, propagating max cumulative latency
        for (auto* node : topoOrder) {
            if (!node) continue;
            const uint32_t id = node->nodeId;
            int maxCumIn = 0;
            for (const auto& c : connections) {
                if (c.destNodeId != id) continue;
                const uint32_t srcId = c.sourceNodeId;
                const int srcCum = cum.count(srcId) ? cum.at(srcId) : 0;
                const int srcLat = nodeLat.count(srcId) ? nodeLat.at(srcId) : 0;
                maxCumIn = std::max(maxCumIn, srcCum + srcLat);
            }
            cum[id] = maxCumIn;
        }

        // ── Step 2: Inject compensators at convergence points ──────
        //
        //  A convergence point is any node with 2+ input connections.
        //  Sum nodes (isSumNode()) are always treated as convergence points.
        //  OUTPUT_NODE_ID is treated as a convergence point for master alignment.

        // Build: dest → list of (srcId, srcChannel)
        std::unordered_map<uint32_t, std::vector<Connection*>> inEdges;
        for (auto& c : connections)
            inEdges[c.destNodeId].push_back(&c);

        // IDs of output nodes: sum nodes + output node
        std::vector<uint32_t> convergencePoints;
        for (auto* n : topoOrder) {
            if (!n) continue;
            if (n->isSumNode() || inEdges[n->nodeId].size() > 1)
                convergencePoints.push_back(n->nodeId);
        }
        // OUTPUT_NODE_ID
        if (inEdges.count(ProcessGraph::OUTPUT_NODE_ID) &&
            inEdges[ProcessGraph::OUTPUT_NODE_ID].size() > 1)
            convergencePoints.push_back(ProcessGraph::OUTPUT_NODE_ID);

        int numCompensators = 0;

        for (uint32_t dstId : convergencePoints) {
            auto& edges = inEdges[dstId];
            if (edges.empty()) continue;

            // Max arriving latency at this convergence point
            int maxArriving = 0;
            for (auto* edge : edges) {
                const uint32_t srcId = edge->sourceNodeId;
                const int srcCum = cum.count(srcId) ? cum.at(srcId) : 0;
                const int srcLat = nodeLat.count(srcId) ? nodeLat.at(srcId) : 0;
                maxArriving = std::max(maxArriving, srcCum + srcLat);
            }

            // For each edge, inject compensator if it arrives too early
            for (auto* edge : edges) {
                const uint32_t srcId = edge->sourceNodeId;
                const int srcCum = cum.count(srcId) ? cum.at(srcId) : 0;
                const int srcLat = nodeLat.count(srcId) ? nodeLat.at(srcId) : 0;
                const int delay  = maxArriving - (srcCum + srcLat);
                if (delay <= 0) continue;

                // Create compensator node
                const int numCh = 2; // stereo
                auto comp = std::make_unique<LatencyCompensatorNode>(delay, numCh);
                if (spec.maxBlockSize > 0) comp->prepare(spec);

                ProcessorNode* compRaw = comp.get();
                const uint32_t compId = compRaw->nodeId = nextNodeId++;

                nodeLat[compId] = 0; // compensator transparent to latency
                cum[compId]     = srcCum + srcLat; // compensator placed after src

                // Rewire: src → comp, comp → dst (same channel indices)
                const int srcCh  = edge->sourceChannelIndex;
                const int dstCh  = edge->destChannelIndex;

                // Replace the original edge: src → dst  becomes  src → comp
                edge->destNodeId          = compId;
                edge->destChannelIndex    = 0;  // comp input ch 0

                // Add new edge: comp → dst
                connections.push_back({ compId, 0, dstId, dstCh });

                // Restore srcCh on input side if needed
                edge->sourceChannelIndex = srcCh;

                nodes.push_back(std::move(comp));
                ++numCompensators;
            }
        }

        return numCompensators;
    }

    // ── debugDump ─────────────────────────────────────────────────
    //  Returns a human-readable graph latency report. Non-RT, debug only.
    static juce::String debugDump(
        const std::vector<ProcessorNode*>& topoOrder,
        const std::vector<Connection>&     connections)
    {
        std::unordered_map<uint32_t, int> nodeLat, cum;
        nodeLat[ProcessGraph::INPUT_NODE_ID] = 0;
        cum    [ProcessGraph::INPUT_NODE_ID] = 0;
        for (auto* n : topoOrder)
            if (n) nodeLat[n->nodeId] = n->getLatencySamples();

        for (auto* n : topoOrder) {
            if (!n) continue;
            int maxIn = 0;
            for (const auto& c : connections) {
                if (c.destNodeId != n->nodeId) continue;
                const int s = cum.count(c.sourceNodeId) ? cum.at(c.sourceNodeId) : 0;
                const int l = nodeLat.count(c.sourceNodeId) ? nodeLat.at(c.sourceNodeId) : 0;
                maxIn = std::max(maxIn, s + l);
            }
            cum[n->nodeId] = maxIn;
        }

        juce::String out = "=== PDC Graph Latency Report ===\n";
        for (auto* n : topoOrder) {
            if (!n) continue;
            out << "  [" << juce::String(n->nodeId) << "] "
                << n->name
                << "  ownLat=" << n->getLatencySamples()
                << "  cumIn=" << cum[n->nodeId]
                << (n->isSumNode() ? "  [SUM]" : "")
                << "\n";
        }
        return out;
    }

    // No static state — nextNodeId is passed by reference from ProcessGraph.
};

} // namespace y2k::dsp
