#pragma once
// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/graph/graph_state.h
//
//  GraphState — immutable snapshot of graph topology.
//
//  The ProcessGraph owns all node objects (via unique_ptr).
//  A GraphState holds a snapshot of the active topology:
//    • execution order (raw pointers into graph's node storage)
//    • connection list (value-type copies)
//    • process spec (sample rate, block size)
//
//  Threading model:
//    UI thread:
//      1. Edits graph (add/remove/connect) — under UI lock.
//      2. Calls ProcessGraph::commitState() to rebuild topology.
//      3. commitState() atomically swaps the active snapshot.
//      4. Old snapshot is destroyed when the UI's shared_ptr drops.
//
//    Audio thread:
//      1. Calls ProcessGraph::acquireState() → shared_ptr<const GraphState>
//         (atomic load, increments ref count once — no malloc).
//      2. Processes using the snapshot.
//      3. shared_ptr drops at end of callback → ref count decrements.
//
//  The ref-count delta in the audio thread is a single atomic add/sub
//  on the control block, which is allocation-free. On x86-64 / arm64
//  with GCC/Clang/MSVC this path is lock-free.
// ═══════════════════════════════════════════════════════════════════

#include "processor_graph.h"   // ProcessorNode, Connection, ProcessSpec
#include <memory>
#include <vector>

namespace y2k::dsp {

struct GraphState {
    // Ordered list of nodes to execute (raw ptrs — nodes owned by ProcessGraph).
    // Do not store or delete through these pointers.
    std::vector<ProcessorNode*>  executionOrder;

    // Full connection list at the time of the snapshot.
    std::vector<Connection>      connections;

    // Spec in effect when this snapshot was built.
    ProcessSpec                  spec;

    // Prevent accidental copy — must go through the factory.
    GraphState() = default;
    GraphState(const GraphState&) = delete;
    GraphState& operator=(const GraphState&) = delete;
    GraphState(GraphState&&) = default;
};

} // namespace y2k::dsp
