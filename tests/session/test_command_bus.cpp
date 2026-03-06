// tests/session/test_command_bus.cpp — v10 (14 cases)
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_engine/session/session_command_bus.h"
using namespace y2k::engine;
using Catch::Approx;

struct BusFixture {
    Y2KSession      session;
    ProjectRouting  routing  = ProjectRouting::createDefault();
    UndoManager     undo     {50};
    SessionCommandBus bus { session, routing, undo, nullptr };
    juce::String trackId() const { return routing.tracks[0].id.toString(); }
};

TEST_CASE("CmdBus: setTrackVolume pushes to UndoManager", "[cmdbus]") {
    BusFixture f;
    f.bus.setTrackVolume(f.trackId(), 0.5f);
    REQUIRE(f.undo.canUndo());
}
TEST_CASE("CmdBus: setTrackVolume mutates routing", "[cmdbus]") {
    BusFixture f;
    f.bus.setTrackVolume(f.trackId(), 0.3f);
    REQUIRE(f.routing.tracks[0].volume == Approx(0.3f));
}
TEST_CASE("CmdBus: undo restores old volume", "[cmdbus]") {
    BusFixture f;
    f.routing.tracks[0].volume = 0.8f;
    f.bus.setTrackVolume(f.trackId(), 0.1f);
    f.undo.undo();
    REQUIRE(f.routing.tracks[0].volume == Approx(0.8f));
}
TEST_CASE("CmdBus: redo re-applies new volume", "[cmdbus]") {
    BusFixture f;
    f.routing.tracks[0].volume = 0.8f;
    f.bus.setTrackVolume(f.trackId(), 0.2f);
    f.undo.undo();
    f.undo.redo();
    REQUIRE(f.routing.tracks[0].volume == Approx(0.2f));
}
TEST_CASE("CmdBus: setTrackPan mutates routing + undoable", "[cmdbus]") {
    BusFixture f;
    f.bus.setTrackPan(f.trackId(), 0.5f);
    REQUIRE(f.routing.tracks[0].pan == Approx(0.5f));
    f.undo.undo();
    REQUIRE(f.routing.tracks[0].pan == Approx(0.f));
}
TEST_CASE("CmdBus: setTrackMuted mutates + undoable", "[cmdbus]") {
    BusFixture f;
    f.bus.setTrackMuted(f.trackId(), true);
    REQUIRE(f.routing.tracks[0].muted == true);
    f.undo.undo();
    REQUIRE(f.routing.tracks[0].muted == false);
}
TEST_CASE("CmdBus: setTrackSoloed mutates + undoable", "[cmdbus]") {
    BusFixture f;
    f.bus.setTrackSoloed(f.trackId(), true);
    REQUIRE(f.routing.tracks[0].soloed == true);
    f.undo.undo();
    REQUIRE(f.routing.tracks[0].soloed == false);
}
TEST_CASE("CmdBus: setTempo mutates session + undoable", "[cmdbus]") {
    BusFixture f; f.session.timeline.tempo = 120.0;
    f.bus.setTempo(140.0);
    REQUIRE(f.session.timeline.tempo == Approx(140.0));
    f.undo.undo();
    REQUIRE(f.session.timeline.tempo == Approx(120.0));
}
TEST_CASE("CmdBus: addTrack adds to routing", "[cmdbus]") {
    BusFixture f;
    const int before = static_cast<int>(f.routing.tracks.size());
    f.bus.addTrack("NewTrack", "SineOsc");
    REQUIRE(static_cast<int>(f.routing.tracks.size()) == before + 1);
}
TEST_CASE("CmdBus: addTrack is undoable", "[cmdbus]") {
    BusFixture f;
    const int before = static_cast<int>(f.routing.tracks.size());
    f.bus.addTrack("TempTrack", "SineOsc");
    f.undo.undo();
    REQUIRE(static_cast<int>(f.routing.tracks.size()) == before);
}
TEST_CASE("CmdBus: removeTrack removes from routing", "[cmdbus]") {
    BusFixture f;
    const int before = static_cast<int>(f.routing.tracks.size());
    f.bus.removeTrack(f.trackId());
    REQUIRE(static_cast<int>(f.routing.tracks.size()) == before - 1);
}
TEST_CASE("CmdBus: removeTrack is undoable", "[cmdbus]") {
    BusFixture f;
    const int before = static_cast<int>(f.routing.tracks.size());
    f.bus.removeTrack(f.trackId());
    f.undo.undo();
    REQUIRE(static_cast<int>(f.routing.tracks.size()) == before);
}
TEST_CASE("CmdBus: onSessionChanged fires on volume change", "[cmdbus]") {
    BusFixture f; bool fired = false;
    f.bus.onSessionChanged = [&]{ fired = true; };
    f.bus.setTrackVolume(f.trackId(), 0.5f);
    REQUIRE(fired);
}
TEST_CASE("CmdBus: onRoutingChanged fires on addTrack", "[cmdbus]") {
    BusFixture f; bool fired = false;
    f.bus.onRoutingChanged = [&]{ fired = true; };
    f.bus.addTrack("X", "SineOsc");
    REQUIRE(fired);
}
