#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_engine/automation_writer.h"
#include "y2k_engine/session.h"

using namespace y2k::engine;
using Catch::Approx;

TEST_CASE("AutomationWriter: generates 2 lanes", "[automation]") {
    Track track;
    track.name = "TestTrack";

    AutomationWriter::generate(track, 1, 2);

    REQUIRE(track.automationLanes.size() == 2);
    REQUIRE(track.automationLanes[0].isActive);
    REQUIRE(track.automationLanes[1].isActive);
    REQUIRE(track.automationLanes[0].targetNodeId == 1);
    REQUIRE(track.automationLanes[1].targetNodeId == 2);
}

TEST_CASE("AutomationWriter: lanes have correct point count", "[automation]") {
    Track track;
    AutomationWriterParams p;
    p.bars         = 8;
    p.pointsPerBar = 4;
    AutomationWriter::generate(track, 10, 20, p);

    // 8 bars * 4 pts + 1 endpoint = 33 points each
    REQUIRE(track.automationLanes[0].points.size() == 33);
    REQUIRE(track.automationLanes[1].points.size() == 33);
}

TEST_CASE("AutomationWriter: lane 0 values in [0,1]", "[automation]") {
    Track track;
    AutomationWriter::generate(track, 1, 2);

    for (const auto& pt : track.automationLanes[0].points) {
        REQUIRE(pt.value >= 0.f);
        REQUIRE(pt.value <= 1.f);
    }
}

TEST_CASE("AutomationPlayer: loads lanes from track", "[automation]") {
    Track track;
    AutomationWriter::generate(track, 1, 2);

    AutomationPlayer player;
    player.loadFromTrack(track);

    REQUIRE(player.hasLanes());
    REQUIRE(player.laneCount() == 2);
}

TEST_CASE("AutomationPlayer: tick does not crash when not playing", "[automation]") {
    Track track;
    AutomationWriter::generate(track, 1, 2);

    AutomationPlayer player;
    player.loadFromTrack(track);

    int callCount = 0;
    auto dispatch = [&](uint32_t, int, float) { ++callCount; };
    player.tick(4.0, false, dispatch);  // isPlaying = false

    REQUIRE(callCount == 0);  // should not dispatch when not playing
}

TEST_CASE("AutomationPlayer: tick dispatches when playing", "[automation]") {
    Track track;
    AutomationWriter::generate(track, 1, 2);

    AutomationPlayer player;
    player.loadFromTrack(track);

    int callCount = 0;
    auto dispatch = [&](uint32_t, int, float) { ++callCount; };
    player.tick(4.0, true, dispatch);  // isPlaying = true

    REQUIRE(callCount == 2);  // 2 lanes = 2 dispatches
}

TEST_CASE("AutomationPlayer: clear removes all lanes", "[automation]") {
    Track track;
    AutomationWriter::generate(track, 1, 2);

    AutomationPlayer player;
    player.loadFromTrack(track);
    REQUIRE(player.hasLanes());
    player.clear();
    REQUIRE(!player.hasLanes());
}
