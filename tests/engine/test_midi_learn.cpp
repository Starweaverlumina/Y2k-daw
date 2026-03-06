#include <catch2/catch_test_macros.hpp>
#include "y2k_engine/midi_learn.h"

using namespace y2k::engine;

static juce::MidiBuffer makeCCBuffer(int channel, int cc, int value) {
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::controllerEvent(channel, cc, value), 0);
    return buf;
}

TEST_CASE("MidiLearnSystem: initially not armed", "[midilearn]") {
    MidiLearnSystem sys;
    REQUIRE(!sys.isArmed());
    REQUIRE(sys.getMappings().empty());
}

TEST_CASE("MidiLearnSystem: armLearn sets armed flag", "[midilearn]") {
    MidiLearnSystem sys;
    sys.armLearn(MidiLearnTargetType::TrackVolume, 0, 0, "Volume");
    REQUIRE(sys.isArmed());
}

TEST_CASE("MidiLearnSystem: cancelLearn clears arm", "[midilearn]") {
    MidiLearnSystem sys;
    sys.armLearn(MidiLearnTargetType::TrackVolume, 0, 0);
    sys.cancelLearn();
    REQUIRE(!sys.isArmed());
}

TEST_CASE("MidiLearnSystem: processMidi binds CC when armed", "[midilearn]") {
    MidiLearnSystem sys;
    sys.armLearn(MidiLearnTargetType::TrackVolume, 1, 0, "Vol");

    auto buf = makeCCBuffer(1, 7, 64);  // CC7 = volume

    int callCount = 0;
    sys.processMidi(buf, [&](const MidiLearnMapping& m, float v) {
        ++callCount;
        REQUIRE(m.ccNumber == 7);
        REQUIRE(v == Catch::Approx(64.f / 127.f).margin(0.01f));
    });

    REQUIRE(!sys.isArmed());
    REQUIRE(sys.getMappings().size() == 1);
    REQUIRE(sys.getMappings()[0].ccNumber == 7);
    REQUIRE(callCount == 1);
}

TEST_CASE("MidiLearnSystem: processMidi dispatches after binding", "[midilearn]") {
    MidiLearnSystem sys;
    sys.armLearn(MidiLearnTargetType::TrackPan, 0, 1, "Pan");

    auto buf1 = makeCCBuffer(1, 10, 0);   // Bind CC10 = pan
    sys.processMidi(buf1, [](const MidiLearnMapping&, float) {});

    REQUIRE(!sys.isArmed());

    float dispatchedValue = -1.f;
    auto buf2 = makeCCBuffer(1, 10, 100);
    sys.processMidi(buf2, [&](const MidiLearnMapping& m, float v) {
        REQUIRE(m.ccNumber == 10);
        dispatchedValue = v;
    });

    REQUIRE(dispatchedValue == Catch::Approx(100.f / 127.f).margin(0.01f));
}

TEST_CASE("MidiLearnSystem: addMapping / removeMapping", "[midilearn]") {
    MidiLearnSystem sys;

    MidiLearnMapping m;
    m.ccNumber  = 7;
    m.channel   = 1;
    m.targetType = MidiLearnTargetType::TrackVolume;
    m.targetId  = 0;
    m.paramId   = 0;
    sys.addMapping(m);

    REQUIRE(sys.getMappings().size() == 1);
    REQUIRE(sys.removeMapping(7, 1));
    REQUIRE(sys.getMappings().empty());
}

TEST_CASE("MidiLearnSystem: clearAllMappings", "[midilearn]") {
    MidiLearnSystem sys;
    for (int i = 0; i < 5; ++i) {
        MidiLearnMapping m;
        m.ccNumber = i;
        sys.addMapping(m);
    }
    REQUIRE(sys.getMappings().size() == 5);
    sys.clearAllMappings();
    REQUIRE(sys.getMappings().empty());
}

TEST_CASE("MidiLearnSystem: ValueTree round-trip", "[midilearn]") {
    MidiLearnSystem sys;
    MidiLearnMapping m;
    m.ccNumber   = 7;
    m.channel    = 1;
    m.targetType = MidiLearnTargetType::TrackVolume;
    m.targetId   = 2;
    m.paramId    = 0;
    m.label      = "Volume";
    sys.addMapping(m);

    auto vt = sys.saveToValueTree();

    MidiLearnSystem sys2;
    sys2.loadFromValueTree(vt);
    REQUIRE(sys2.getMappings().size() == 1);
    REQUIRE(sys2.getMappings()[0].ccNumber == 7);
    REQUIRE(sys2.getMappings()[0].label == "Volume");
}
