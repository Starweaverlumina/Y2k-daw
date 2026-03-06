// tests/session/test_auto_save_manager.cpp — v10 (11 cases)
#include <catch2/catch_test_macros.hpp>
#include "y2k_engine/session/auto_save_manager.h"
using namespace y2k::engine;

TEST_CASE("AutoSave: starts not dirty", "[autosave]") {
    AutoSaveManager mgr(1);
    REQUIRE_FALSE(mgr.isDirty());
}
TEST_CASE("AutoSave: markDirty sets dirty", "[autosave]") {
    AutoSaveManager mgr(1);
    mgr.markDirty();
    REQUIRE(mgr.isDirty());
}
TEST_CASE("AutoSave: cancelDirty clears dirty", "[autosave]") {
    AutoSaveManager mgr(1);
    mgr.markDirty(); mgr.cancelDirty();
    REQUIRE_FALSE(mgr.isDirty());
}
TEST_CASE("AutoSave: onFullSaveCompleted clears dirty", "[autosave]") {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("y2k_autosave_test_dir");
    tmp.createDirectory();
    AutoSaveManager mgr(60);
    mgr.markDirty();
    mgr.onFullSaveCompleted();
    REQUIRE_FALSE(mgr.isDirty());
    tmp.deleteRecursively();
}
TEST_CASE("AutoSave: onFullSaveCompleted removes autosave file", "[autosave]") {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("y2k_autosave_test_dir2");
    tmp.createDirectory();
    tmp.getChildFile("autosave.xml").replaceWithText("<test/>");
    Y2KSession sess; ProjectRouting rout = ProjectRouting::createDefault();
    AutoSaveManager mgr(60);
    mgr.start(tmp, [&]{ return &sess; }, [&]{ return &rout; });
    mgr.onFullSaveCompleted();
    mgr.stop();
    REQUIRE_FALSE(tmp.getChildFile("autosave.xml").existsAsFile());
    tmp.deleteRecursively();
}
TEST_CASE("AutoSave: recoveryAvailable false when no autosave", "[autosave]") {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("y2k_recv_test");
    tmp.createDirectory();
    REQUIRE_FALSE(AutoSaveManager::recoveryAvailable(tmp));
    tmp.deleteRecursively();
}
TEST_CASE("AutoSave: recoveryAvailable true when no project.xml", "[autosave]") {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("y2k_recv_test2");
    tmp.createDirectory();
    tmp.getChildFile("autosave.xml").replaceWithText("<Y2KSession/>");
    REQUIRE(AutoSaveManager::recoveryAvailable(tmp));
    tmp.deleteRecursively();
}
TEST_CASE("AutoSave: discardRecovery removes autosave.xml", "[autosave]") {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("y2k_discard");
    tmp.createDirectory();
    tmp.getChildFile("autosave.xml").replaceWithText("<test/>");
    AutoSaveManager::discardRecovery(tmp);
    REQUIRE_FALSE(tmp.getChildFile("autosave.xml").existsAsFile());
    tmp.deleteRecursively();
}
TEST_CASE("AutoSave: autoSaveFile path is bundle/autosave.xml", "[autosave]") {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("y2k_aspath");
    tmp.createDirectory();
    Y2KSession sess; ProjectRouting rout = ProjectRouting::createDefault();
    AutoSaveManager mgr(60);
    mgr.start(tmp, [&]{ return &sess; }, [&]{ return &rout; });
    mgr.stop();
    REQUIRE(mgr.autoSaveFile() == tmp.getChildFile("autosave.xml"));
    tmp.deleteRecursively();
}
TEST_CASE("AutoSave: lastSaveTime initially invalid", "[autosave]") {
    AutoSaveManager mgr(60);
    REQUIRE(mgr.lastSaveTime() == juce::Time());
}
TEST_CASE("AutoSave: lastSaveFailed false initially", "[autosave]") {
    AutoSaveManager mgr(60);
    REQUIRE_FALSE(mgr.lastSaveFailed());
}
