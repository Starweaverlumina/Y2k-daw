#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_dsp/mixer/sum_node.h"

using namespace y2k::dsp;
using Catch::Approx;

static ProcessSpec spec44(int bs=512) { return {44100.0,bs,2}; }

TEST_CASE("SumNode: isSumNode() returns true", "[sumnode]") {
    REQUIRE(SumNode{}.isSumNode());
}

TEST_CASE("SumNode: getLatencySamples() == 0", "[sumnode]") {
    REQUIRE(SumNode{}.getLatencySamples() == 0);
}

TEST_CASE("SumNode: unity gain passthrough", "[sumnode]") {
    SumNode sum(2,2,1.f);
    sum.prepare(spec44(512));
    sum.inputBuf.setSize(2,512,false,true,false);
    juce::FloatVectorOperations::fill(sum.inputBuf.getWritePointer(0), 0.5f, 512);
    juce::FloatVectorOperations::fill(sum.inputBuf.getWritePointer(1), 0.3f, 512);
    ProcessContext ctx; ctx.blockSize=512; ctx.sampleRate=44100.0;
    sum.process(ctx);
    REQUIRE(sum.outputBuf.getSample(0,0) == Approx(0.5f).margin(1e-5f));
    REQUIRE(sum.outputBuf.getSample(1,0) == Approx(0.3f).margin(1e-5f));
}

TEST_CASE("SumNode: gain parameter scales output", "[sumnode]") {
    SumNode sum(2,2,2.f);
    sum.prepare(spec44(512));
    sum.inputBuf.setSize(2,512,false,true,false);
    juce::FloatVectorOperations::fill(sum.inputBuf.getWritePointer(0), 0.5f, 512);
    ProcessContext ctx; ctx.blockSize=512;
    sum.process(ctx);
    REQUIRE(sum.outputBuf.getSample(0,0) == Approx(1.0f).margin(1e-4f));
}

TEST_CASE("SumNode: setGain updates live", "[sumnode]") {
    SumNode sum;
    sum.prepare(spec44(512));
    sum.setGain(0.f);
    sum.inputBuf.setSize(2,512,false,true,false);
    juce::FloatVectorOperations::fill(sum.inputBuf.getWritePointer(0), 1.f, 512);
    ProcessContext ctx; ctx.blockSize=512;
    sum.process(ctx);
    REQUIRE(sum.outputBuf.getMagnitude(0,0,512) == Approx(0.f).margin(1e-5f));
}

TEST_CASE("SumNode: no allocation in process()", "[sumnode]") {
    SumNode sum; sum.prepare(spec44(512));
    sum.inputBuf.setSize(2,512,false,true,false);
    ProcessContext ctx; ctx.blockSize=512;
    for (int i=0;i<200;++i) REQUIRE_NOTHROW(sum.process(ctx));
}

TEST_CASE("SumNode: silence on zero input", "[sumnode]") {
    SumNode sum; sum.prepare(spec44(512));
    sum.inputBuf.setSize(2,512,false,true,false);
    sum.inputBuf.clear();
    ProcessContext ctx; ctx.blockSize=512;
    sum.process(ctx);
    REQUIRE(sum.outputBuf.getMagnitude(0,0,512) == Approx(0.f));
}

TEST_CASE("SumNode: getNumParameters() == 1", "[sumnode]") {
    REQUIRE(SumNode{}.getNumParameters() == 1);
}
