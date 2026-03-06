#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_dsp/graph/processor_graph.h"
#include "y2k_dsp/generators/sine_osc.h"

using namespace y2k::dsp;
using Catch::Approx;

static ProcessSpec spec44() { return {44100.0, 512, 2}; }

TEST_CASE("ProcessGraph: addNode assigns unique IDs", "[graph]") {
    ProcessGraph g;
    auto* n1 = g.addNode(std::make_unique<SineOscProcessor>());
    auto* n2 = g.addNode(std::make_unique<SineOscProcessor>());
    REQUIRE(n1 != nullptr);
    REQUIRE(n2 != nullptr);
    REQUIRE(n1->nodeId != n2->nodeId);
}

TEST_CASE("ProcessGraph: getNode finds by ID", "[graph]") {
    ProcessGraph g;
    auto* n = g.addNode(std::make_unique<SineOscProcessor>());
    REQUIRE(g.getNode(n->nodeId) == n);
    REQUIRE(g.getNode(9999) == nullptr);
}

TEST_CASE("ProcessGraph: connect succeeds for valid nodes", "[graph]") {
    ProcessGraph g;
    auto* src = g.addNode(std::make_unique<SineOscProcessor>());
    const bool ok = g.connect({src->nodeId, 0, ProcessGraph::OUTPUT_NODE_ID, 0});
    REQUIRE(ok);
}

TEST_CASE("ProcessGraph: connect fails for duplicate", "[graph]") {
    ProcessGraph g;
    auto* src = g.addNode(std::make_unique<SineOscProcessor>());
    g.connect({src->nodeId, 0, ProcessGraph::OUTPUT_NODE_ID, 0});
    const bool dup = g.connect({src->nodeId, 0, ProcessGraph::OUTPUT_NODE_ID, 0});
    REQUIRE(!dup);
}

TEST_CASE("ProcessGraph: disconnect removes connection", "[graph]") {
    ProcessGraph g;
    auto* src = g.addNode(std::make_unique<SineOscProcessor>());
    Connection c = {src->nodeId, 0, ProcessGraph::OUTPUT_NODE_ID, 0};
    g.connect(c);
    REQUIRE(g.getConnections().size() == 1);
    g.disconnect(c);
    REQUIRE(g.getConnections().empty());
}

TEST_CASE("ProcessGraph: removeNode also removes its connections", "[graph]") {
    ProcessGraph g;
    auto* n = g.addNode(std::make_unique<SineOscProcessor>());
    g.connect({n->nodeId, 0, ProcessGraph::OUTPUT_NODE_ID, 0});
    g.removeNode(n->nodeId);
    REQUIRE(g.getConnections().empty());
    REQUIRE(g.getNode(n->nodeId) == nullptr);
}

TEST_CASE("ProcessGraph: prepare commits state (non-null)", "[graph]") {
    ProcessGraph g;
    g.addNode(std::make_unique<SineOscProcessor>());
    g.prepare(spec44());
    auto state = g.acquireState();
    REQUIRE(state != nullptr);
    REQUIRE(!state->executionOrder.empty());
}

TEST_CASE("ProcessGraph: processBlock produces audio after prepare", "[graph]") {
    ProcessGraph g;
    auto* src = g.addNode(std::make_unique<SineOscProcessor>(440.f, 0.5f));
    g.connect({src->nodeId, 0, ProcessGraph::OUTPUT_NODE_ID, 0});
    g.connect({src->nodeId, 1, ProcessGraph::OUTPUT_NODE_ID, 1});
    g.prepare(spec44());

    juce::AudioBuffer<float> audio(2, 512);
    audio.clear();
    juce::MidiBuffer midi;
    ProcessContext ctx;
    ctx.blockSize = 512; ctx.sampleRate = 44100.0;
    g.processBlock(audio, midi, ctx);

    REQUIRE(audio.getMagnitude(0, 0, 512) > 0.f);
}

TEST_CASE("ProcessGraph: pushParameterEvent layer 0 sets base", "[graph]") {
    ProcessGraph g;
    auto* n = g.addNode(std::make_unique<SineOscProcessor>(440.f, 0.1f));
    g.prepare(spec44());
    ParameterEvent evt;
    evt.nodeId=n->nodeId; evt.paramId=0; evt.layer=0; evt.value=880.f;
    g.pushParameterEvent(evt);

    juce::AudioBuffer<float> audio(2,512); juce::MidiBuffer midi;
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    g.processBlock(audio, midi, ctx);

    REQUIRE(n->getParameter(0) == Approx(880.f));
}

TEST_CASE("ProcessGraph: pushParameterEvent layer 1 automation doesn't change base", "[graph]") {
    ProcessGraph g;
    auto* n = g.addNode(std::make_unique<SineOscProcessor>(440.f, 0.1f));
    g.prepare(spec44());

    const float base = n->getParameter(0);

    ParameterEvent evt;
    evt.nodeId=n->nodeId; evt.paramId=0; evt.layer=1; evt.value=220.f;
    g.pushParameterEvent(evt);

    juce::AudioBuffer<float> audio(2,512); juce::MidiBuffer midi;
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    g.processBlock(audio, midi, ctx);

    REQUIRE(n->getParameter(0)     == Approx(base));    // base unchanged
    REQUIRE(n->resolvedParameter(0) > base);            // automation added
}

TEST_CASE("ProcessGraph: clear resets graph", "[graph]") {
    ProcessGraph g;
    g.addNode(std::make_unique<SineOscProcessor>());
    g.prepare(spec44());
    g.clear();
    REQUIRE(g.getNodeIds().empty());
    REQUIRE(g.acquireState() == nullptr);
}
