#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_dsp/param/parameter_state.h"

using namespace y2k::dsp;
using Catch::Approx;

TEST_CASE("ParameterState: default resolved is base", "[param]") {
    ParameterState p;
    p.setBase(0.5f);
    REQUIRE(p.resolved() == Approx(0.5f));
    REQUIRE(p.automation == 0.f);
    REQUIRE(p.modulation == 0.f);
    REQUIRE(p.midi       == 0.f);
}

TEST_CASE("ParameterState: automation adds to resolved but not base", "[param]") {
    ParameterState p;
    p.setBase(0.4f);
    p.setAutomation(0.2f);
    REQUIRE(p.resolved() == Approx(0.6f));
    REQUIRE(p.getBase()  == Approx(0.4f));  // base unchanged
}

TEST_CASE("ParameterState: midi adds to resolved but not base", "[param]") {
    ParameterState p;
    p.setBase(0.3f);
    p.setMidi(0.1f);
    REQUIRE(p.resolved() == Approx(0.4f));
    REQUIRE(p.getBase()  == Approx(0.3f));
}

TEST_CASE("ParameterState: sum is clamped to [min, max]", "[param]") {
    ParameterState p;
    p.minValue = 0.f; p.maxValue = 1.f;
    p.setBase(0.9f);
    p.setAutomation(0.5f);
    REQUIRE(p.resolved() == Approx(1.0f)); // clamped at max
    p.setAutomation(-2.0f);
    REQUIRE(p.resolved() == Approx(0.0f)); // clamped at min
}

TEST_CASE("ParameterState: all layers combine", "[param]") {
    ParameterState p;
    p.minValue = 0.f; p.maxValue = 2.f;
    p.setBase(0.5f);
    p.setAutomation(0.3f);
    p.setModulation(0.1f);
    p.setMidi(0.1f);
    REQUIRE(p.resolved() == Approx(1.0f)); // 0.5+0.3+0.1+0.1
}

TEST_CASE("ParameterState: snapshot round-trip", "[param]") {
    ParameterState p;
    p.setBase(0.77f);
    p.minValue = 0.2f; p.maxValue = 0.9f;
    const auto snap = p.snapshot();
    ParameterState p2;
    p2.restore(snap);
    REQUIRE(p2.getBase()   == Approx(0.77f));
    REQUIRE(p2.minValue    == Approx(0.2f));
    REQUIRE(p2.maxValue    == Approx(0.9f));
}

TEST_CASE("ParameterBank: initialise and index", "[param]") {
    ParameterBank bank;
    bank.initialise(4);
    REQUIRE(bank.size() == 4);
    bank.setBase(0, 0.5f);
    bank.setBase(1, 0.25f);
    REQUIRE(bank.getBase(0) == Approx(0.5f));
    REQUIRE(bank.getBase(1) == Approx(0.25f));
}

TEST_CASE("ParameterBank: automation does not overwrite base", "[param]") {
    ParameterBank bank;
    bank.initialise(2);
    bank.setBase(0, 0.4f);
    bank.setAutomation(0, 0.2f);
    REQUIRE(bank.getBase(0)    == Approx(0.4f));
    REQUIRE(bank.resolved(0)   == Approx(0.6f));
}

TEST_CASE("ParameterBank: midi layer", "[param]") {
    ParameterBank bank;
    bank.initialise(1);
    bank.setBase(0, 0.3f);
    bank.setMidi(0, 0.1f);
    REQUIRE(bank.resolved(0) == Approx(0.4f));
    REQUIRE(bank.getBase(0)  == Approx(0.3f));
}

TEST_CASE("ParameterBank: out-of-range access is safe", "[param]") {
    ParameterBank bank;
    bank.initialise(2);
    REQUIRE_NOTHROW(bank.setBase(-1, 0.5f));
    REQUIRE_NOTHROW(bank.setBase(99, 0.5f));
    REQUIRE(bank.resolved(-1) == Approx(0.f));
    REQUIRE(bank.resolved(99) == Approx(0.f));
}
