#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_engine/graph_synth/graph_compiler.h"
#include "y2k_engine/mixer/track_model.h"
#include "y2k_dsp/meter/meter_tap.h"

using namespace y2k::engine;
using namespace y2k::dsp;
using Catch::Approx;

static ProcessSpec spec44(int bs=512) { return {44100.0,bs,2}; }

TEST_CASE("GraphCompiler: default routing compiles without error", "[compiler]") {
    auto routing = ProjectRouting::createDefault();
    auto result = GraphCompiler::compile(routing);
    // Graph should have nodes
    REQUIRE(!result.graph.getNodeIds().empty());
}

TEST_CASE("GraphCompiler: produces one master meter tap", "[compiler]") {
    auto routing = ProjectRouting::createDefault();
    auto result = GraphCompiler::compile(routing);
    REQUIRE(result.masterMeter != nullptr);
}

TEST_CASE("GraphCompiler: produces one meter per track", "[compiler]") {
    auto routing = ProjectRouting::createDefault();
    auto result = GraphCompiler::compile(routing);
    REQUIRE(result.trackMeters.size() == routing.tracks.size());
}

TEST_CASE("GraphCompiler: graph has master SumNode (isSumNode)", "[compiler]") {
    auto routing = ProjectRouting::createDefault();
    auto result = GraphCompiler::compile(routing);
    bool hasSumNode = false;
    for (uint32_t id : result.graph.getNodeIds()) {
        auto* n = result.graph.getNode(id);
        if (n && n->isSumNode()) { hasSumNode = true; break; }
    }
    REQUIRE(hasSumNode);
}

TEST_CASE("GraphCompiler: compileAndPrepare produces audio", "[compiler]") {
    auto routing = ProjectRouting::createDefault();
    auto result = GraphCompiler::compile(routing);
    result.graph.prepare(spec44(512));

    juce::AudioBuffer<float> audio(2, 512); audio.clear();
    juce::MidiBuffer midi;
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    result.graph.processBlock(audio, midi, ctx);

    // SineOsc instruments will produce non-zero audio
    REQUIRE(audio.getMagnitude(0,0,512) > 0.f);
}

TEST_CASE("GraphCompiler: compiled track IDs are non-zero", "[compiler]") {
    auto routing = ProjectRouting::createDefault();
    auto result = GraphCompiler::compile(routing);
    for (const auto& t : routing.tracks) {
        // Instrument node should have been assigned
        REQUIRE(t.compiled.instrumentNodeId != 0);
        REQUIRE(t.compiled.faderNodeId      != 0);
        REQUIRE(t.compiled.meterTapNodeId   != 0);
    }
}

TEST_CASE("GraphCompiler: bus routing — track with outputBus routes to bus sum", "[compiler]") {
    auto routing = ProjectRouting::createDefault();
    // Add a bus
    BusModel bus; bus.id = juce::Uuid(); bus.name = "Group A";
    routing.buses.push_back(bus);
    // Route track 0 to this bus
    routing.tracks[0].outputBusId = routing.buses.back().id;

    auto result = GraphCompiler::compile(routing);
    REQUIRE(result.busMeters.size() == 1);
    REQUIRE(!result.busMeters.empty());
}

TEST_CASE("GraphCompiler: post-fader send tap node created", "[compiler]") {
    auto routing = ProjectRouting::createDefault();
    BusModel bus; bus.id = juce::Uuid(); bus.name = "FX Bus";
    routing.buses.push_back(bus);
    SendSlot send; send.targetBusId = routing.buses.back().id; send.level=0.5f; send.preFader=false;
    routing.tracks[0].sends.push_back(send);

    auto result = GraphCompiler::compile(routing);
    // Send tap node ID should be set
    REQUIRE(!routing.tracks[0].compiled.sendTapNodeIds.empty());
}

TEST_CASE("GraphCompiler: PDC runs on prepare+commitState — revision advances", "[compiler]") {
    auto routing = ProjectRouting::createDefault();
    auto result = GraphCompiler::compile(routing);
    result.graph.prepare(spec44());
    const uint64_t rev = result.graph.graphRevision();
    result.graph.commitState();
    REQUIRE(result.graph.graphRevision() > rev);
}

TEST_CASE("GraphCompiler: master meter reads non-zero after audio", "[compiler]") {
    auto routing = ProjectRouting::createDefault();
    auto result = GraphCompiler::compile(routing);
    result.graph.prepare(spec44(512));

    juce::AudioBuffer<float> audio(2,512); audio.clear();
    juce::MidiBuffer midi;
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    result.graph.processBlock(audio, midi, ctx);

    REQUIRE(result.masterMeter != nullptr);
    REQUIRE(result.masterMeter->read().peakL > 0.f);
}
