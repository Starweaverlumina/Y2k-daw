#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_dsp/graph/processor_graph.h"
#include "y2k_dsp/graph/graph_state.h"
#include "y2k_dsp/generators/sine_osc.h"

using namespace y2k::dsp;
using Catch::Approx;

TEST_CASE("GraphState: acquireState returns nullptr before prepare", "[graphstate]") {
    ProcessGraph g;
    auto state = g.acquireState();
    REQUIRE(state == nullptr);
}

TEST_CASE("GraphState: acquireState non-null after prepare", "[graphstate]") {
    ProcessGraph g;
    g.addNode(std::make_unique<SineOscProcessor>());
    g.prepare({44100.0, 512, 2});
    auto state = g.acquireState();
    REQUIRE(state != nullptr);
}

TEST_CASE("GraphState: snapshot has correct execution order size", "[graphstate]") {
    ProcessGraph g;
    g.addNode(std::make_unique<SineOscProcessor>());
    g.addNode(std::make_unique<SineOscProcessor>());
    g.prepare({44100.0, 512, 2});
    auto state = g.acquireState();
    REQUIRE(state->executionOrder.size() == 2);
}

TEST_CASE("GraphState: old state outlives commitState (ref counting)", "[graphstate]") {
    ProcessGraph g;
    g.addNode(std::make_unique<SineOscProcessor>());
    g.prepare({44100.0, 512, 2});
    auto oldState = g.acquireState();
    REQUIRE(oldState != nullptr);
    // Commit new state — oldState still valid via shared_ptr
    g.addNode(std::make_unique<SineOscProcessor>());
    g.commitState();
    auto newState = g.acquireState();
    REQUIRE(newState != oldState);
    REQUIRE(oldState.use_count() == 1); // only our ptr remains
    REQUIRE(newState->executionOrder.size() == 2);
}

TEST_CASE("GraphState: ParameterEvent layer 1 updates automation, not base", "[graphstate]") {
    ProcessGraph g;
    auto* node = g.addNode(std::make_unique<SineOscProcessor>(440.f, 0.1f));
    g.prepare({44100.0, 512, 2});

    const float baseFreq = node->getParameter(0);

    // Push automation event (layer=1)
    ParameterEvent evt;
    evt.nodeId  = node->nodeId;
    evt.paramId = 0;
    evt.layer   = 1;  // automation
    evt.value   = 880.f;
    g.pushParameterEvent(evt);

    // Process one block to drain queue
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    ProcessContext ctx;
    ctx.blockSize = 512; ctx.sampleRate = 44100.0;
    g.processBlock(audio, midi, ctx);

    // Base must be unchanged; resolved must include automation offset
    REQUIRE(node->getParameter(0) == Approx(baseFreq)); // base unchanged
    REQUIRE(node->resolvedParameter(0) > baseFreq);     // automation added
}

TEST_CASE("GraphState: ParameterEvent layer 0 sets base", "[graphstate]") {
    ProcessGraph g;
    auto* node = g.addNode(std::make_unique<SineOscProcessor>(440.f, 0.1f));
    g.prepare({44100.0, 512, 2});

    ParameterEvent evt;
    evt.nodeId  = node->nodeId;
    evt.paramId = 0;
    evt.layer   = 0;  // base
    evt.value   = 660.f;
    g.pushParameterEvent(evt);

    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    g.processBlock(audio, midi, ctx);

    REQUIRE(node->getParameter(0) == Approx(660.f));
}
