#include <catch2/catch_test_macros.hpp>
#include "y2k_engine/mixer/track_model.h"

using namespace y2k::engine;

TEST_CASE("ProjectRouting: createDefault returns 4 tracks + master", "[trackmodel]") {
    auto r = ProjectRouting::createDefault();
    REQUIRE(r.tracks.size() == 4);
    REQUIRE(!r.masterBus.id.isNull());
    REQUIRE(r.masterBus.isMaster);
}

TEST_CASE("ProjectRouting: validate passes on default", "[trackmodel]") {
    auto r = ProjectRouting::createDefault();
    REQUIRE(r.validate().wasOk());
}

TEST_CASE("ProjectRouting: validate detects null masterBus ID", "[trackmodel]") {
    ProjectRouting r;
    r.masterBus.id = juce::Uuid::null();
    REQUIRE_FALSE(r.validate().wasOk());
}

TEST_CASE("ProjectRouting: validate detects duplicate track IDs", "[trackmodel]") {
    auto r = ProjectRouting::createDefault();
    r.tracks[1].id = r.tracks[0].id;
    REQUIRE_FALSE(r.validate().wasOk());
}

TEST_CASE("ProjectRouting: validate detects send to unknown bus", "[trackmodel]") {
    auto r = ProjectRouting::createDefault();
    SendSlot s; s.targetBusId = juce::Uuid(); // random unknown UUID
    r.tracks[0].sends.push_back(s);
    REQUIRE_FALSE(r.validate().wasOk());
}

TEST_CASE("ProjectRouting: findBus returns masterBus by ID", "[trackmodel]") {
    auto r = ProjectRouting::createDefault();
    REQUIRE(r.findBus(r.masterBus.id) == &r.masterBus);
}

TEST_CASE("ProjectRouting: findBus returns nullptr for unknown ID", "[trackmodel]") {
    auto r = ProjectRouting::createDefault();
    REQUIRE(r.findBus(juce::Uuid()) == nullptr);
}

TEST_CASE("TrackModel: default instrument track to master (no outputBusId)", "[trackmodel]") {
    auto r = ProjectRouting::createDefault();
    for (const auto& t : r.tracks)
        REQUIRE_FALSE(t.outputBusId.has_value());
}

TEST_CASE("BusModel: default bus not master", "[trackmodel]") {
    BusModel b; b.id = juce::Uuid();
    REQUIRE_FALSE(b.isMaster);
}
