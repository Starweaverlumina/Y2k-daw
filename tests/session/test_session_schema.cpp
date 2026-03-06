// tests/session/test_session_schema.cpp — v10 (18 cases)
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "y2k_engine/session/session_schema.h"
using namespace y2k::engine;
using Catch::Approx;

static std::unique_ptr<Y2KSession> ms() { return Y2KSession::createEmpty(); }
static ProjectRouting mr()               { return ProjectRouting::createDefault(); }

TEST_CASE("Schema: serialise produces valid Y2KSession tree", "[schema]") {
    auto s=ms(); auto r=mr();
    auto vt = SessionSchema::serialise(*s, r);
    REQUIRE(vt.isValid());
    REQUIRE(vt.getType().toString() == "Y2KSession");
}
TEST_CASE("Schema: schemaVersion is 2.0", "[schema]") {
    auto vt = SessionSchema::serialise(*ms(), mr());
    REQUIRE(SessionSchema::schemaVersion(vt) == "2.0");
}
TEST_CASE("Schema: isCompatible returns true", "[schema]") {
    REQUIRE(SessionSchema::isCompatible(SessionSchema::serialise(*ms(), mr())));
}
TEST_CASE("Schema: round-trip preserves projectName", "[schema]") {
    auto s=ms(); s->projectName="Bondi Beats"; auto r=mr();
    auto lr = SessionSchema::deserialise(SessionSchema::serialise(*s, r));
    REQUIRE(lr.status.wasOk());
    REQUIRE(lr.session->projectName == "Bondi Beats");
}
TEST_CASE("Schema: round-trip preserves tempo", "[schema]") {
    auto s=ms(); s->timeline.tempo=98.7; auto r=mr();
    auto lr = SessionSchema::deserialise(SessionSchema::serialise(*s, r));
    REQUIRE(lr.session->timeline.tempo == Approx(98.7));
}
TEST_CASE("Schema: round-trip preserves time signature", "[schema]") {
    auto s=ms(); s->timeline.timeSigNum=3; s->timeline.timeSigDen=8; auto r=mr();
    auto lr = SessionSchema::deserialise(SessionSchema::serialise(*s, r));
    REQUIRE(lr.session->timeline.timeSigNum == 3);
    REQUIRE(lr.session->timeline.timeSigDen == 8);
}
TEST_CASE("Schema: round-trip preserves master bus name", "[schema]") {
    auto s=ms(); auto r=mr(); r.masterBus.name="MyMaster";
    auto lr = SessionSchema::deserialise(SessionSchema::serialise(*s, r));
    REQUIRE(lr.routing.masterBus.name == "MyMaster");
}
TEST_CASE("Schema: round-trip preserves track volume", "[schema]") {
    auto s=ms(); auto r=mr(); r.tracks[0].volume=0.42f;
    auto lr = SessionSchema::deserialise(SessionSchema::serialise(*s, r));
    REQUIRE(lr.routing.tracks[0].volume == Approx(0.42f));
}
TEST_CASE("Schema: round-trip preserves track pan", "[schema]") {
    auto s=ms(); auto r=mr(); r.tracks[0].pan=-0.3f;
    auto lr = SessionSchema::deserialise(SessionSchema::serialise(*s, r));
    REQUIRE(lr.routing.tracks[0].pan == Approx(-0.3f));
}
TEST_CASE("Schema: round-trip preserves track mute", "[schema]") {
    auto s=ms(); auto r=mr(); r.tracks[0].muted=true;
    auto lr = SessionSchema::deserialise(SessionSchema::serialise(*s, r));
    REQUIRE(lr.routing.tracks[0].muted == true);
}
TEST_CASE("Schema: round-trip preserves send (level+preFader)", "[schema]") {
    auto s=ms(); auto r=mr();
    BusModel bus; bus.id=juce::Uuid(); bus.name="Reverb"; r.buses.push_back(bus);
    SendSlot send; send.targetBusId=r.buses.back().id; send.level=0.5f; send.preFader=true;
    r.tracks[0].sends.push_back(send);
    auto lr = SessionSchema::deserialise(SessionSchema::serialise(*s, r));
    REQUIRE(lr.routing.tracks[0].sends.size() == 1);
    REQUIRE(lr.routing.tracks[0].sends[0].preFader == true);
    REQUIRE(lr.routing.tracks[0].sends[0].level == Approx(0.5f));
}
TEST_CASE("Schema: deserialise returns fail for invalid tree", "[schema]") {
    juce::ValueTree bad("NotASession");
    auto lr = SessionSchema::deserialise(bad);
    REQUIRE_FALSE(lr.status.wasOk());
}
TEST_CASE("Schema: migrate v1.0 adds schemaVersion=2.0", "[schema]") {
    juce::ValueTree root("Y2KSession");
    root.appendChild(juce::ValueTree("Tracks"), nullptr);
    auto log = SessionSchema::migrate(root);
    REQUIRE(SessionSchema::schemaVersion(root) == "2.0");
    REQUIRE(log.isNotEmpty());
}
TEST_CASE("Schema: migrate v1.0 adds Routing subtree", "[schema]") {
    juce::ValueTree root("Y2KSession");
    root.appendChild(juce::ValueTree("Tracks"), nullptr);
    SessionSchema::migrate(root);
    REQUIRE(root.getChildWithName("Routing").isValid());
}
TEST_CASE("Schema: migrate is idempotent on v2.0", "[schema]") {
    auto vt = SessionSchema::serialise(*ms(), mr());
    REQUIRE(SessionSchema::migrate(vt).isEmpty());
}
TEST_CASE("Schema: atomicSave creates file, no .tmp left", "[schema]") {
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("y2k_schema_test.xml");
    f.deleteFile();
    REQUIRE(SessionSchema::atomicSave(*ms(), mr(), f).wasOk());
    REQUIRE(f.existsAsFile());
    REQUIRE_FALSE(f.getSiblingFile("y2k_schema_test.tmp").existsAsFile());
    f.deleteFile();
}
TEST_CASE("Schema: atomicSave+deserialise round-trip via file", "[schema]") {
    auto s=ms(); s->projectName="FileRT"; s->timeline.tempo=133.3;
    auto f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("y2k_rt.xml");
    f.deleteFile();
    REQUIRE(SessionSchema::atomicSave(*s, mr(), f).wasOk());
    auto xml = juce::XmlDocument::parse(f); REQUIRE(xml != nullptr);
    auto lr = SessionSchema::deserialise(juce::ValueTree::fromXml(*xml));
    REQUIRE(lr.session->projectName == "FileRT");
    REQUIRE(lr.session->timeline.tempo == Approx(133.3));
    f.deleteFile();
}
TEST_CASE("Schema: autoSaveIsNewer returns false when no autosave", "[schema]") {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto main = tmp.getChildFile("y2k_main_x.xml");
    auto autosv = tmp.getChildFile("y2k_auto_x.xml");
    main.replaceWithText("<test/>"); autosv.deleteFile();
    REQUIRE_FALSE(SessionSchema::autoSaveIsNewer(main, autosv));
    main.deleteFile();
}
