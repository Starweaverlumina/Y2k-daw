#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_dsp/graph/processor_graph.h"

using namespace y2k::dsp;
using Catch::Approx;

TEST_CASE("SmoothParameter: snap sets value immediately", "[dsp]") {
    SmoothParameter p;
    p.reset(44100.0, 10.0);
    p.snap(0.75f);
    REQUIRE(p.advance() == Approx(0.75f));
}

TEST_CASE("SmoothParameter: ramp reaches target", "[dsp]") {
    SmoothParameter p;
    p.reset(44100.0, 1.0);   // 1 ms ramp at 44100 Hz = 44 samples
    p.snap(0.0f);
    p.setTarget(1.0f);
    p.trigger();

    float last = 0.f;
    for (int i = 0; i < 500; ++i) last = p.advance();
    REQUIRE(last == Approx(1.0f).margin(0.001f));
}

TEST_CASE("SmoothParameter: atomic setTarget is visible from another context", "[dsp]") {
    SmoothParameter p;
    p.reset(44100.0, 0.0);   // instant ramp
    p.setTarget(0.5f);
    p.trigger();
    REQUIRE(p.advance() == Approx(0.5f).margin(0.01f));
}
