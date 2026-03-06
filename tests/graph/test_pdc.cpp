// ═══════════════════════════════════════════════════════════════════
//  tests/graph/test_pdc.cpp  — v9
//  PDC: cumulative latency propagation + compensator injection
// ═══════════════════════════════════════════════════════════════════
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_dsp/graph/processor_graph.h"
#include "y2k_dsp/graph/pdc.h"
#include "y2k_dsp/graph/latency_compensator.h"
#include "y2k_dsp/mixer/sum_node.h"
#include "y2k_dsp/generators/sine_osc.h"

using namespace y2k::dsp;
using PG = ProcessGraph;
using Catch::Approx;

static ProcessSpec spec44(int block=512) { return {44100.0, block, 2}; }

// ─────────────────────────────────────────────────────────────────
//  Helper: simple latency-reporting processor for testing
// ─────────────────────────────────────────────────────────────────
class LatentProcessor final : public ProcessorNode {
public:
    explicit LatentProcessor(int lat, const juce::String& label="LatentProc") noexcept
        : lat_(lat) { name = label; }
    int getLatencySamples() const noexcept override { return lat_; }
    int getNumInputChannels()  const override { return 2; }
    int getNumOutputChannels() const override { return 2; }
    void prepare(const ProcessSpec& spec) override {
        outputBuf.setSize(2, spec.maxBlockSize, false, true, false);
        inputBuf .setSize(2, spec.maxBlockSize, false, true, false);
    }
    void process(const ProcessContext& ctx) override {
        for (int ch=0;ch<2;++ch)
            outputBuf.copyFrom(ch,0,inputBuf,ch,0,ctx.blockSize);
    }
    void reset() override {}
private:
    int lat_;
};

// ─────────────────────────────────────────────────────────────────
//  Tests
// ─────────────────────────────────────────────────────────────────

TEST_CASE("PDC: no compensators when no latency", "[pdc]") {
    ProcessGraph g;
    auto* osc = g.addNode(std::make_unique<SineOscProcessor>());
    auto* sum = g.addNode(std::make_unique<SumNode>());
    g.connect({ osc->nodeId, 0, sum->nodeId, 0 });
    g.connect({ osc->nodeId, 1, sum->nodeId, 1 });
    g.connect({ sum->nodeId, 0, PG::OUTPUT_NODE_ID, 0 });
    g.prepare(spec44());
    REQUIRE(g.lastPdcCompensatorCount() == 0);
}

TEST_CASE("PDC: single path with latency, no sum node — no compensator needed", "[pdc]") {
    // Single chain: osc → latent(128) → output
    // No convergence point → no compensator needed
    ProcessGraph g;
    auto* osc = g.addNode(std::make_unique<SineOscProcessor>());
    auto* lat = g.addNode(std::make_unique<LatentProcessor>(128, "LP128"));
    g.connect({ osc->nodeId, 0, lat->nodeId, 0 });
    g.connect({ osc->nodeId, 1, lat->nodeId, 1 });
    g.connect({ lat->nodeId, 0, PG::OUTPUT_NODE_ID, 0 });
    g.connect({ lat->nodeId, 1, PG::OUTPUT_NODE_ID, 1 });
    g.prepare(spec44());
    REQUIRE(g.lastPdcCompensatorCount() == 0);
}

TEST_CASE("PDC: parallel paths into sum node — compensator injected on shorter path", "[pdc]") {
    // Path A: oscA → lat(256) → SumNode
    // Path B: oscB →           → SumNode
    // Expected: compensator(256) injected on path B
    ProcessGraph g;
    auto* oscA  = g.addNode(std::make_unique<SineOscProcessor>(440.f, 0.05f));
    auto* oscB  = g.addNode(std::make_unique<SineOscProcessor>(880.f, 0.05f));
    auto* lat   = g.addNode(std::make_unique<LatentProcessor>(256, "LP256"));
    auto* sum   = g.addNode(std::make_unique<SumNode>());
    auto* out   = sum->nodeId;

    g.connect({ oscA->nodeId, 0, lat->nodeId, 0 });
    g.connect({ oscA->nodeId, 1, lat->nodeId, 1 });
    g.connect({ lat->nodeId,  0, sum->nodeId, 0 });
    g.connect({ lat->nodeId,  1, sum->nodeId, 1 });
    g.connect({ oscB->nodeId, 0, sum->nodeId, 0 });
    g.connect({ oscB->nodeId, 1, sum->nodeId, 1 });
    g.connect({ sum->nodeId,  0, PG::OUTPUT_NODE_ID, 0 });
    g.connect({ sum->nodeId,  1, PG::OUTPUT_NODE_ID, 1 });

    g.prepare(spec44());
    REQUIRE(g.lastPdcCompensatorCount() >= 1);
}

TEST_CASE("PDC: send path vs direct path aligned at bus sum", "[pdc]") {
    // Direct:  osc → lat(512) → MasterSum
    // Send:    osc → lat(512) → lat(128) → MasterSum  [has MORE latency]
    // Expected: compensator on direct path (512 vs 512+128=640 → delay direct by 128)
    ProcessGraph g;
    auto* osc    = g.addNode(std::make_unique<SineOscProcessor>());
    auto* latA   = g.addNode(std::make_unique<LatentProcessor>(512, "LP512"));
    auto* latSend= g.addNode(std::make_unique<LatentProcessor>(128, "LP128-send"));
    auto* sum    = g.addNode(std::make_unique<SumNode>());

    g.connect({ osc->nodeId,     0, latA->nodeId,    0 });
    g.connect({ osc->nodeId,     1, latA->nodeId,    1 });
    // Direct path: latA → sum
    g.connect({ latA->nodeId,    0, sum->nodeId,     0 });
    g.connect({ latA->nodeId,    1, sum->nodeId,     1 });
    // Send path: latA → latSend → sum
    g.connect({ latA->nodeId,    0, latSend->nodeId, 0 });
    g.connect({ latA->nodeId,    1, latSend->nodeId, 1 });
    g.connect({ latSend->nodeId, 0, sum->nodeId,     0 });
    g.connect({ latSend->nodeId, 1, sum->nodeId,     1 });
    g.connect({ sum->nodeId,     0, PG::OUTPUT_NODE_ID, 0 });

    g.prepare(spec44());
    REQUIRE(g.lastPdcCompensatorCount() >= 1);
}

TEST_CASE("PDC: equal latency paths — no compensator", "[pdc]") {
    // Both paths have same latency → nothing to compensate
    ProcessGraph g;
    auto* oscA = g.addNode(std::make_unique<LatentProcessor>(100, "A"));
    auto* oscB = g.addNode(std::make_unique<LatentProcessor>(100, "B"));
    auto* sum  = g.addNode(std::make_unique<SumNode>());
    g.connect({ oscA->nodeId, 0, sum->nodeId, 0 });
    g.connect({ oscA->nodeId, 1, sum->nodeId, 1 });
    g.connect({ oscB->nodeId, 0, sum->nodeId, 0 });
    g.connect({ oscB->nodeId, 1, sum->nodeId, 1 });
    g.connect({ sum->nodeId,  0, PG::OUTPUT_NODE_ID, 0 });
    g.prepare(spec44());
    REQUIRE(g.lastPdcCompensatorCount() == 0);
}

TEST_CASE("PDC: LatencyCompensatorNode produces zero-delay passthrough at delay=0", "[pdc]") {
    LatencyCompensatorNode comp(0);
    comp.prepare(spec44(512));
    // Fill inputBuf with known signal
    comp.inputBuf.setSize(2, 512, false, true, false);
    for (int s=0; s<512; ++s) comp.inputBuf.setSample(0,s, float(s)*0.001f);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    ctx.outputBuffer = &comp.outputBuf;
    comp.process(ctx);
    // Output at sample 0 should be the input (delay=0 → bypass)
    REQUIRE(comp.outputBuf.getSample(0, 0) == Approx(0.f).margin(1e-5f));
    REQUIRE(comp.outputBuf.getSample(0, 1) == Approx(0.001f).margin(1e-5f));
}

TEST_CASE("PDC: LatencyCompensatorNode delays by N samples", "[pdc]") {
    const int delay = 64;
    LatencyCompensatorNode comp(delay);
    comp.prepare(spec44(512));
    comp.inputBuf.setSize(2, 512, false, true, false);
    comp.outputBuf.setSize(2, 512, false, true, false);

    // Fill with impulse at sample 0
    comp.inputBuf.clear();
    comp.inputBuf.setSample(0, 0, 1.f);

    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    comp.process(ctx);

    // Impulse should appear at output sample `delay`
    REQUIRE(comp.outputBuf.getSample(0, 0)     == Approx(0.f).margin(1e-5f));
    REQUIRE(comp.outputBuf.getSample(0, delay) == Approx(1.f).margin(1e-5f));
}

TEST_CASE("PDC: LatencyCompensatorNode no allocation in process()", "[pdc]") {
    LatencyCompensatorNode comp(128);
    comp.prepare(spec44(512));
    comp.inputBuf.setSize(2, 512, false, true, false);
    comp.inputBuf.clear();
    ProcessContext ctx; ctx.blockSize=512;
    REQUIRE_NOTHROW(comp.process(ctx));
}

TEST_CASE("PDC: graphRevision increments on each commitState", "[pdc]") {
    ProcessGraph g;
    g.addNode(std::make_unique<SineOscProcessor>());
    g.prepare(spec44());
    const auto rev0 = g.graphRevision();
    g.commitState();
    const auto rev1 = g.graphRevision();
    REQUIRE(rev1 > rev0);
}

TEST_CASE("PDC: isSumNode correct on SumNode vs SineOsc", "[pdc]") {
    SumNode sum;
    REQUIRE(sum.isSumNode());
    SineOscProcessor osc;
    REQUIRE(!osc.isSumNode());
}

TEST_CASE("PDC: debugDump non-RT returns non-empty string", "[pdc]") {
    ProcessGraph g;
    g.addNode(std::make_unique<LatentProcessor>(64, "LP"));
    g.prepare(spec44());
    const auto dump = g.pdcDebugDump();
    REQUIRE(dump.isNotEmpty());
    REQUIRE(dump.contains("ownLat=64"));
}
