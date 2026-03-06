#include <catch2/catch_test_macros.hpp>
#include "y2k_engine/undo.h"

using namespace y2k::engine;

struct CounterAction : public UndoAction {
    int& counter; int delta;
    CounterAction(int& c, int d) : counter(c), delta(d) {}
    void undo() override { counter -= delta; }
    void redo() override { counter += delta; }
    juce::String description() const override { return "Counter+" + juce::String(delta); }
};

TEST_CASE("UndoManager: perform/undo/redo", "[undo]") {
    UndoManager mgr; int c = 0;
    mgr.perform(std::make_unique<CounterAction>(c, 5));
    REQUIRE(c == 5);
    mgr.undo(); REQUIRE(c == 0);
    mgr.redo(); REQUIRE(c == 5);
}

TEST_CASE("UndoManager: stack of actions", "[undo]") {
    UndoManager mgr; int c = 0;
    mgr.perform(std::make_unique<CounterAction>(c, 1));
    mgr.perform(std::make_unique<CounterAction>(c, 2));
    mgr.perform(std::make_unique<CounterAction>(c, 4));
    REQUIRE(c == 7);
    mgr.undo(); REQUIRE(c == 3);
    mgr.undo(); REQUIRE(c == 1);
    mgr.redo(); REQUIRE(c == 3);
}

TEST_CASE("UndoManager: new action clears redo", "[undo]") {
    UndoManager mgr; int c = 0;
    mgr.perform(std::make_unique<CounterAction>(c, 10));
    mgr.undo(); REQUIRE(mgr.canRedo());
    mgr.perform(std::make_unique<CounterAction>(c, 3));
    REQUIRE(!mgr.canRedo());
    REQUIRE(c == 3);
}

TEST_CASE("UndoManager: maxHistory cap", "[undo]") {
    UndoManager mgr(3); int c = 0;
    for (int i = 0; i < 5; ++i)
        mgr.perform(std::make_unique<CounterAction>(c, 1));
    REQUIRE(mgr.getHistorySize() == 3);
}

TEST_CASE("UndoManager: clear", "[undo]") {
    UndoManager mgr; int c = 0;
    mgr.perform(std::make_unique<CounterAction>(c, 5));
    mgr.clear();
    REQUIRE(!mgr.canUndo()); REQUIRE(!mgr.canRedo());
}

TEST_CASE("UndoManager: LambdaUndoAction", "[undo]") {
    UndoManager mgr; int v = 0;
    mgr.perform(std::make_unique<LambdaUndoAction>(
        "Set42", [&]{ v = 0; }, [&]{ v = 42; }));
    REQUIRE(v == 42);
    mgr.undo(); REQUIRE(v == 0);
    mgr.redo(); REQUIRE(v == 42);
}

TEST_CASE("UndoManager: revertTo", "[undo]") {
    UndoManager mgr; int c = 0;
    mgr.perform(std::make_unique<CounterAction>(c, 1));
    mgr.perform(std::make_unique<CounterAction>(c, 10));
    mgr.perform(std::make_unique<CounterAction>(c, 100));
    REQUIRE(c == 111);
    mgr.revertTo(0); REQUIRE(c == 1);
    mgr.revertTo(2); REQUIRE(c == 111);
}

TEST_CASE("UndoManager: onChange fires", "[undo]") {
    UndoManager mgr; int calls = 0; int c = 0;
    mgr.onChange = [&]{ ++calls; };
    mgr.perform(std::make_unique<CounterAction>(c, 1)); REQUIRE(calls == 1);
    mgr.undo();                                          REQUIRE(calls == 2);
    mgr.redo();                                          REQUIRE(calls == 3);
}
