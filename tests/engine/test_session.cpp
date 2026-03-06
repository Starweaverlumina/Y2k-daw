#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_engine/session.h"

using namespace y2k::engine;
using Catch::Approx;

TEST_CASE("Y2KSession: createEmpty produces valid session", "[session]") {
    auto s = Y2KSession::createEmpty();
    REQUIRE(s != nullptr);
    REQUIRE(s->isValid());
    REQUIRE(s->tracks.empty());
    REQUIRE(s->projectName == "Untitled");
}

TEST_CASE("Y2KSession: createFromTemplate produces 8 tracks", "[session]") {
    auto s = Y2KSession::createFromTemplate("Test");
    REQUIRE(s->tracks.size() == 8);
    REQUIRE(s->tracks[0]->name == "Bondi Bass");
    REQUIRE(s->tracks[4]->name == "Flower Drums");
    REQUIRE(s->tracks[7]->name == "Bondi Aux");
}

TEST_CASE("Y2KSession: addTrack / removeTrack", "[session]") {
    auto s = Y2KSession::createEmpty();
    auto* t = s->addTrack(Track::Type::Audio, "Test Track");
    const juce::Uuid id = t->id;
    REQUIRE(s->tracks.size() == 1);
    REQUIRE(s->removeTrack(id));
    REQUIRE(s->tracks.empty());
    REQUIRE(!s->removeTrack(id));
}

TEST_CASE("Y2KSession: reorderTrack", "[session]") {
    auto s = Y2KSession::createEmpty();
    s->addTrack(Track::Type::Audio, "A");
    s->addTrack(Track::Type::Audio, "B");
    s->addTrack(Track::Type::Audio, "C");
    s->reorderTrack(0, 2);
    REQUIRE(s->tracks[0]->name == "B");
    REQUIRE(s->tracks[2]->name == "A");
}

TEST_CASE("Y2KSession: save and load round-trip", "[session]") {
    auto s = Y2KSession::createFromTemplate("RoundTrip");
    s->timeline.tempo = 140.0;
    s->author = "TestAuthor";

    auto clip = std::make_unique<MidiClip>();
    clip->name = "Test Clip";
    clip->startBeat = 4.0;
    clip->durationBeats = 8.0;
    clip->notes.push_back({ 60, 0.8f, 0.0, 1.0 });
    s->tracks[0]->clips.push_back(std::move(clip));

    const juce::File tmpDir = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getChildFile("y2k_test_bundle.y2k");
    tmpDir.deleteRecursively();
    REQUIRE(s->save(tmpDir).wasOk());

    auto s2 = std::make_unique<Y2KSession>();
    REQUIRE(s2->load(tmpDir).wasOk());
    REQUIRE(s2->projectName == "RoundTrip");
    REQUIRE(s2->timeline.tempo == Approx(140.0));
    REQUIRE(s2->tracks.size() == 8);
    REQUIRE(s2->tracks[0]->clips.size() == 1);
    REQUIRE(s2->tracks[0]->clips[0]->name == "Test Clip");

    tmpDir.deleteRecursively();
}

TEST_CASE("Y2KSession: beatsToSeconds / secondsToBeats", "[session]") {
    auto s = Y2KSession::createEmpty();
    s->timeline.tempo = 120.0;
    REQUIRE(s->beatsToSeconds(4.0) == Approx(2.0));
    REQUIRE(s->secondsToBeats(2.0) == Approx(4.0));
}
