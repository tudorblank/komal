#include "command/actions.hpp"
#include "helpers.hpp"

#include <doctest/doctest.h>

TEST_CASE("arguments are checked against their spec")
{
    const ArgSpec specs[] = { Arg::id("node"), Arg::number("value", 0.0, 1.0), Arg::choice("mode", { "paint", "erase" }),
                              Arg::flag("on").optional(), Arg::ids("nodes").optional() };
    CHECK(checkArgs(specs, { { "node", 3 }, { "value", 0.5 }, { "mode", "paint" } }).empty());
    CHECK(checkArgs(specs, { { "node", 3 }, { "value", 1 }, { "mode", "erase" }, { "on", true } }).empty());   // an integer reads as a number
    CHECK_FALSE(checkArgs(specs, { { "value", 0.5 }, { "mode", "paint" } }).empty());                          // missing
    CHECK_FALSE(checkArgs(specs, { { "node", 0 }, { "value", 0.5 }, { "mode", "paint" } }).empty());           // not an id
    CHECK_FALSE(checkArgs(specs, { { "node", 3 }, { "value", 1.5 }, { "mode", "paint" } }).empty());           // out of range
    CHECK_FALSE(checkArgs(specs, { { "node", 3 }, { "value", 0.5 }, { "mode", "blur" } }).empty());            // not a choice
    CHECK_FALSE(checkArgs(specs, { { "node", 3 }, { "value", 0.5 }, { "mode", "paint" }, { "extra", 1 } }).empty());   // unknown
    CHECK_FALSE(checkArgs(specs, { { "node", 3 }, { "value", 0.5 }, { "mode", "paint" }, { "on", 1 } }).empty());      // not a flag
    CHECK_FALSE(checkArgs(specs, { { "node", 3 }, { "value", 0.5 }, { "mode", "paint" }, { "nodes", std::vector<int64_t>{ 2, 0 } } }).empty());
}

TEST_CASE("the registry refuses malformed actions and bad arguments")
{
    ActionRegistry r;
    int runs = 0;
    r.add({ .id = ActionId("test.run"), .title = "Run", .kind = ActionKind::Interface, .args = { Arg::integer("n", 0, 10) },
            .run = [&](const ActionArgs& a) { runs += int(a.integer("n")); } });
    CHECK(r.find(ActionId("test.run")));
    CHECK(r.run(ActionId("test.run"), { { "n", 2 } }));
    const LogCapture log;
    CHECK_FALSE(r.run(ActionId("test.run"), { { "n", 20 } }));   // refused: out of range
    CHECK(log.saw("'n' is out of range"));
    CHECK(runs == 2);
}

TEST_CASE("a gesture's end sees begin's arguments, and a cancel ends it")
{
    ActionRegistry r;
    int began = 0, ended = 0, cancelled = 0;
    int64_t seen = 0;
    r.add({ .id = ActionId("test.drag"), .title = "Drag", .kind = ActionKind::Gesture, .args = { Arg::id("node") },
            .endArgs = { Arg::number("dx") },
            .begin  = [&] { ++began; },
            .end    = [&](const ActionArgs& a) { ++ended; seen = a.integer("node"); },
            .cancel = [&] { ++cancelled; } });
    REQUIRE(r.beginGesture(ActionId("test.drag"), { { "node", 7 } }));
    CHECK(r.inGesture(ActionId("test.drag")));
    CHECK(r.endGesture(ActionId("test.drag"), { { "dx", 1.0 } }));
    CHECK(seen == 7);
    REQUIRE(r.beginGesture(ActionId("test.drag"), { { "node", 7 } }));
    r.cancelGesture(ActionId("test.drag"));
    CHECK_FALSE(r.inGesture(ActionId("test.drag")));
    CHECK(began == 2);
    CHECK(ended == 1);
    CHECK(cancelled == 1);
}

TEST_CASE("a gesture's updates are checked: a mistaken one is refused, the gesture goes on")
{
    ActionRegistry r;
    std::vector<double> seen;
    r.add({ .id = ActionId("test.scrub"), .title = "Scrub", .kind = ActionKind::Gesture, .args = { Arg::id("node") },
            .updateArgs = { Arg::number("value", 0.0, 1.0) },
            .begin  = [] { return true; },
            .update = [&](const ActionArgs& a) { seen.push_back(a.number("value")); },
            .end    = [] {} });
    REQUIRE(r.beginGesture(ActionId("test.scrub"), { { "node", 1 } }));
    r.updateGesture(ActionId("test.scrub"), { { "value", 0.25 } });
    {
        const LogCapture log;
        r.updateGesture(ActionId("test.scrub"), { { "value", 4.0 } });   // out of range
        r.updateGesture(ActionId("test.scrub"), { { "other", 1 } });     // not its argument
        CHECK(log.saw("refused"));
    }
    CHECK(r.inGesture(ActionId("test.scrub")));
    r.updateGesture(ActionId("test.scrub"), { { "value", 0.5 } });
    CHECK(seen == std::vector<double>{ 0.25, 0.5 });
    CHECK(r.endGesture(ActionId("test.scrub")));
}

TEST_CASE("a text argument can be checked by a rule")
{
    const ArgSpec specs[] = { Arg::text("slot").checked([](std::string_view t) { return t == "input"; }) };
    CHECK(checkArgs(specs, { { "slot", "input" } }).empty());
    CHECK_FALSE(checkArgs(specs, { { "slot", "layer:9x" } }).empty());
}
