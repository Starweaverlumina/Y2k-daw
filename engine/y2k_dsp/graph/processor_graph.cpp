// ═══════════════════════════════════════════════════════════════════
//  engine/y2k_dsp/graph/processor_graph.cpp  — v8
//  Atomic GraphState swap. Audio thread never locks.
// ═══════════════════════════════════════════════════════════════════
#include "processor_graph.h"
#include "graph_state.h"
#include "pdc.h"
#include "latency_compensator.h"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace y2k::dsp {

namespace {

bool sameConn(const Connection& a, const Connection& b) noexcept {
    return a.sourceNodeId==b.sourceNodeId && a.sourceChannelIndex==b.sourceChannelIndex
        && a.destNodeId==b.destNodeId     && a.destChannelIndex==b.destChannelIndex;
}
int safeCh(int ch, int total) noexcept { return (ch>=0&&ch<total)?ch:-1; }

} // namespace

ProcessGraph::ProcessGraph()  = default;
ProcessGraph::~ProcessGraph() = default;

// ─────────────────────────────────────────────────────────────────
//  addNode  [non-RT]
// ─────────────────────────────────────────────────────────────────
ProcessorNode* ProcessGraph::addNode(NodePtr node) {
    if (!node) return nullptr;
    node->nodeId = nextNodeId_++;
    if (node->name.isEmpty())
        node->name = "Node " + juce::String(int(node->nodeId));

    if (currentSpec_.maxBlockSize > 0) {
        node->prepare(currentSpec_);
        node->inputBuf .setSize(std::max(1,node->getNumInputChannels()),
                                currentSpec_.maxBlockSize,false,true,false);
        node->outputBuf.setSize(std::max(1,node->getNumOutputChannels()),
                                currentSpec_.maxBlockSize,false,true,false);
        node->inputBuf.clear(); node->outputBuf.clear();
    }
    ProcessorNode* raw = node.get();
    nodes_.push_back(std::move(node));
    return raw;
}

bool ProcessGraph::removeNode(uint32_t id) {
    const auto before = nodes_.size();
    nodes_.erase(std::remove_if(nodes_.begin(),nodes_.end(),
        [id](const NodePtr& n){return n&&n->nodeId==id;}),nodes_.end());
    if (nodes_.size()==before) return false;
    connections_.erase(std::remove_if(connections_.begin(),connections_.end(),
        [id](const Connection& c){return c.sourceNodeId==id||c.destNodeId==id;}),
        connections_.end());
    return true;
}

bool ProcessGraph::connect(const Connection& c) {
    if (c.sourceNodeId==c.destNodeId) return false;
    const bool sv=(c.sourceNodeId==INPUT_NODE_ID);
    const bool dv=(c.destNodeId==OUTPUT_NODE_ID);
    const ProcessorNode* src=sv?nullptr:getNode(c.sourceNodeId);
    const ProcessorNode* dst=dv?nullptr:getNode(c.destNodeId);
    if(!sv&&!src) return false;
    if(!dv&&!dst) return false;
    if(src&&(c.sourceChannelIndex<0||c.sourceChannelIndex>=src->getNumOutputChannels()))return false;
    if(dst&&(c.destChannelIndex<0  ||c.destChannelIndex  >=dst->getNumInputChannels())) return false;
    if(std::any_of(connections_.begin(),connections_.end(),[&](const Connection& e){return sameConn(e,c);}))return false;
    connections_.push_back(c);
    return true;
}

bool ProcessGraph::disconnect(const Connection& c) {
    const auto before=connections_.size();
    connections_.erase(std::remove_if(connections_.begin(),connections_.end(),
        [&](const Connection& e){return sameConn(e,c);}),connections_.end());
    return connections_.size()<before;
}

void ProcessGraph::clear() {
    nodes_.clear(); connections_.clear(); nextNodeId_=1;
    activeState_.store(nullptr, std::memory_order_release);
}

ProcessorNode* ProcessGraph::getNode(uint32_t id) {
    for(auto& n:nodes_) if(n&&n->nodeId==id) return n.get();
    return nullptr;
}
const ProcessorNode* ProcessGraph::getNode(uint32_t id) const {
    for(const auto& n:nodes_) if(n&&n->nodeId==id) return n.get();
    return nullptr;
}
std::vector<uint32_t> ProcessGraph::getNodeIds() const {
    std::vector<uint32_t> ids; ids.reserve(nodes_.size());
    for(const auto& n:nodes_) if(n) ids.push_back(n->nodeId);
    return ids;
}
const std::vector<Connection>& ProcessGraph::getConnections() const { return connections_; }

// ─────────────────────────────────────────────────────────────────
//  rebuildExecutionOrder  [Kahn topological sort — non-RT]
// ─────────────────────────────────────────────────────────────────
void ProcessGraph::rebuildExecutionOrder(std::vector<ProcessorNode*>& outOrder) const {
    outOrder.clear();
    if (nodes_.empty()) return;

    std::unordered_map<uint32_t,ProcessorNode*>        nodeMap;
    std::unordered_map<uint32_t,int>                   indegree;
    std::unordered_map<uint32_t,std::vector<uint32_t>> edges;
    nodeMap.reserve(nodes_.size()); indegree.reserve(nodes_.size());

    for(const auto& n:nodes_){ if(!n) continue; nodeMap[n->nodeId]=n.get(); indegree[n->nodeId]=0; }
    for(const auto& c:connections_) {
        if(c.sourceNodeId==INPUT_NODE_ID||c.destNodeId==OUTPUT_NODE_ID) continue;
        if(!nodeMap.count(c.sourceNodeId)||!nodeMap.count(c.destNodeId)) continue;
        edges[c.sourceNodeId].push_back(c.destNodeId);
        indegree[c.destNodeId]++;
    }
    std::queue<uint32_t> q;
    for(const auto& kv:indegree) if(kv.second==0) q.push(kv.first);
    std::unordered_set<uint32_t> emitted; emitted.reserve(nodes_.size());
    while(!q.empty()){
        auto id=q.front(); q.pop();
        emitted.insert(id); outOrder.push_back(nodeMap.at(id));
        auto it=edges.find(id);
        if(it!=edges.end()) for(auto dst:it->second) if(--indegree[dst]==0) q.push(dst);
    }
    if(emitted.size()!=nodes_.size()){
        juce::Logger::writeToLog("[y2k] Graph cycle detected — appending remaining nodes");
        for(const auto& n:nodes_) if(n&&!emitted.count(n->nodeId)) outOrder.push_back(n.get());
    }
}

// ─────────────────────────────────────────────────────────────────
//  commitState  [non-RT]
//
//  v9 sequence:
//    1. Rebuild topo order (Kahn) on current nodes/connections
//    2. Run PDC: inject LatencyCompensatorNodes where needed
//    3. Rebuild topo order again (compensators are new nodes)
//    4. Build immutable GraphState
//    5. Atomic swap
//
//  PDCEngine::computeAndInject() may add nodes and connections.
//  All additions are non-RT (ring buffer alloc in prepare()).
//  Audio thread holds previous GraphState until block completes.
// ─────────────────────────────────────────────────────────────────
void ProcessGraph::commitState() {
    // Step 1: Initial topo sort
    std::vector<ProcessorNode*> initialOrder;
    rebuildExecutionOrder(initialOrder);

    // Step 2: PDC injection (may add nodes + connections to nodes_/connections_)
    PDCEngine::setNextNodeId(nextNodeId_);
    const int numComp = PDCEngine::computeAndInject(
        initialOrder, nodes_, connections_, currentSpec_);
    // Sync: PDCEngine may have used IDs in [nextNodeId_, nextNodeId_+numComp)
    // The static nextNodeId_ in PDCEngine was set to our nextNodeId_ before call.
    // After injection, advance past all allocated compensator IDs.
    nextNodeId_ = std::max(nextNodeId_, uint32_t(0xFF00'0000u + numComp + 1));
    lastPdcCount_.store(numComp, std::memory_order_release);

    // Step 3: Re-sort with compensators included
    auto newState = std::make_shared<GraphState>();
    rebuildExecutionOrder(newState->executionOrder);
    newState->connections = connections_;
    newState->spec        = currentSpec_;

    // Bump revision counter
    graphRevision_.fetch_add(1, std::memory_order_release);

    activeState_.store(newState, std::memory_order_release);
}

// ─────────────────────────────────────────────────────────────────
//  pdcDebugDump  [non-RT — for UI/logging only]
// ─────────────────────────────────────────────────────────────────
juce::String ProcessGraph::pdcDebugDump() const {
    std::vector<ProcessorNode*> order;
    rebuildExecutionOrder(order);
    return PDCEngine::debugDump(order, connections_);
}

// ─────────────────────────────────────────────────────────────────
//  prepare  [non-RT]
// ─────────────────────────────────────────────────────────────────
void ProcessGraph::prepare(const ProcessSpec& spec) {
    currentSpec_ = spec;
    for(auto& n:nodes_){
        if(!n) continue;
        n->prepare(spec);
        n->inputBuf .setSize(std::max(1,n->getNumInputChannels()),
                             std::max(1,spec.maxBlockSize),false,true,false);
        n->outputBuf.setSize(std::max(1,n->getNumOutputChannels()),
                             std::max(1,spec.maxBlockSize),false,true,false);
        n->inputBuf.clear(); n->outputBuf.clear();
    }
    commitState();
}

// ─────────────────────────────────────────────────────────────────
//  pushParameterEvent / acquireState
// ─────────────────────────────────────────────────────────────────
void ProcessGraph::pushParameterEvent(const ParameterEvent& evt) {
    paramQueue_.push(evt);  // drops silently if full
}

std::shared_ptr<const GraphState> ProcessGraph::acquireState() const noexcept {
    return activeState_.load(std::memory_order_acquire);
}

// ─────────────────────────────────────────────────────────────────
//  drainParameterQueue  [RT — called at start of processBlock]
//
//  Applies ParameterEvents to nodes via layered setters:
//    layer 0 → setParameter  (base, atomic store)
//    layer 1 → setParameterAutomation  (audio thread only)
//    layer 2 → (modulation, future)
//    layer 3 → setParameterMidi        (audio thread only)
//
//  No allocation. Linear scan over executionOrder is fine for
//  typical graphs (< 64 nodes, queue drains in microseconds).
// ─────────────────────────────────────────────────────────────────
void ProcessGraph::drainParameterQueue() {
    const auto state = activeState_.load(std::memory_order_acquire);
    if (!state) return;

    while (auto evt = paramQueue_.pop()) {
        for (ProcessorNode* n : state->executionOrder) {
            if (!n || n->nodeId != evt->nodeId) continue;
            const int p = int(evt->paramId);
            if (p >= n->getNumParameters()) break;
            switch (evt->layer) {
                case 0: n->setParameter(p, evt->value);          break;
                case 1: n->setParameterAutomation(p, evt->value); break;
                case 3: n->setParameterMidi(p, evt->value);       break;
                default: break;
            }
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────
//  processBlock  [RT — no allocation, no locks]
//
//  Acquires snapshot once (shared_ptr ref-count bump, allocation-free).
//  All buffer ops use pre-sized buffers from prepare().
// ─────────────────────────────────────────────────────────────────
void ProcessGraph::processBlock(juce::AudioBuffer<float>& audio,
                                juce::MidiBuffer&         midi,
                                const ProcessContext&     ctxIn)
{
    auto state = activeState_.load(std::memory_order_acquire);
    if (!state) { audio.clear(); return; }

    drainParameterQueue();

    const int N = audio.getNumSamples();
    const int hostChans = audio.getNumChannels();
    if (N<=0||hostChans<=0) return;

    for (ProcessorNode* node : state->executionOrder) {
        if (!node) continue;
        if (node->inputBuf .getNumChannels()>0) node->inputBuf .clear(0,N);
        if (node->outputBuf.getNumChannels()>0) node->outputBuf.clear(0,N);
    }

    for (ProcessorNode* node : state->executionOrder) {
        if (!node) continue;
        for (const Connection& c : state->connections) {
            if (c.destNodeId!=node->nodeId) continue;
            const int dstCh=safeCh(c.destChannelIndex,node->inputBuf.getNumChannels());
            if (dstCh<0) continue;
            float* dst=node->inputBuf.getWritePointer(dstCh);
            if (c.sourceNodeId==INPUT_NODE_ID) {
                const int srcCh=safeCh(c.sourceChannelIndex,hostChans);
                if (srcCh>=0) juce::FloatVectorOperations::add(dst,audio.getReadPointer(srcCh),N);
                continue;
            }
            // Find source in execution order (linear — RT safe, no map)
            for (ProcessorNode* src : state->executionOrder) {
                if (!src||src->nodeId!=c.sourceNodeId) continue;
                const int srcCh=safeCh(c.sourceChannelIndex,src->outputBuf.getNumChannels());
                if (srcCh>=0) juce::FloatVectorOperations::add(dst,src->outputBuf.getReadPointer(srcCh),N);
                break;
            }
        }

        if (node->isBypassed) {
            const int cc=std::min(node->inputBuf.getNumChannels(),node->outputBuf.getNumChannels());
            for (int ch=0;ch<cc;++ch) node->outputBuf.copyFrom(ch,0,node->inputBuf,ch,0,N);
            continue;
        }
        ProcessContext ctx = ctxIn;
        ctx.inputBuffer  = &node->inputBuf;
        ctx.outputBuffer = &node->outputBuf;
        ctx.midiBuffer   = &midi;
        ctx.blockSize    = N;
        ctx.sampleRate   = state->spec.sampleRate;
        node->process(ctx);
    }

    audio.clear();
    for (const Connection& c : state->connections) {
        if (c.destNodeId!=OUTPUT_NODE_ID) continue;
        if (c.sourceNodeId==INPUT_NODE_ID) continue;
        const int dstCh=safeCh(c.destChannelIndex,hostChans);
        if (dstCh<0) continue;
        for (ProcessorNode* src : state->executionOrder) {
            if (!src||src->nodeId!=c.sourceNodeId) continue;
            const int srcCh=safeCh(c.sourceChannelIndex,src->outputBuf.getNumChannels());
            if (srcCh>=0) juce::FloatVectorOperations::add(
                audio.getWritePointer(dstCh),src->outputBuf.getReadPointer(srcCh),N);
            break;
        }
    }
}

} // namespace y2k::dsp
