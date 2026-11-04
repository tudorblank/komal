#include "document/history.hpp"

#include <doctest/doctest.h>

TEST_CASE("the history is a tree: a step after undoing starts a branch")
{
    History h;
    h.push({ "A", nullptr, {} });
    const StepId a = h.current();
    h.push({ "B", nullptr, {} });
    const StepId b = h.current();
    CHECK(h.parent(b) == a);

    h.goTo(a);   // undo B
    CHECK(h.current() == a);
    CHECK(h.redoChild(a) == b);

    h.push({ "C", nullptr, {} });   // a branch: B stays
    const StepId c = h.current();
    CHECK(h.children(a).size() == 2);
    CHECK(h.redoChild(a) == c);

    h.goTo(b);   // across branches, straight there
    CHECK(h.current() == b);
    CHECK(h.canUndo());
    CHECK_FALSE(h.canRedo());
    h.goTo(c);   // and back: redo from A comes back the way it left, to B
    CHECK(h.redoChild(a) == b);
}

TEST_CASE("steps removed for good: their children take their place, the rest numbered again")
{
    // 0 Start - 1 A - 2 B - 3 C          (and from A: 4 D - 5 E)
    History h;
    h.push({ "A", nullptr, {} });
    h.push({ "B", nullptr, {} });
    h.push({ "C", nullptr, {} });
    h.goTo(StepId(1));
    h.push({ "D", nullptr, {} });
    h.push({ "E", nullptr, {} });
    h.goTo(StepId(3));   // at C

    h.remove({ StepId(2), StepId(4) });   // B in the middle, D a branch's first step
    // Now: 0 Start - 1 A - 2 C, and from A: 3 E (E is its branch's first now)
    REQUIRE(h.size() == 4);
    CHECK(h.step(StepId(2)).label == "C");
    CHECK(h.step(StepId(3)).label == "E");
    CHECK(h.parent(StepId(2)) == StepId(1));
    CHECK(h.parent(StepId(3)) == StepId(1));
    CHECK(h.children(StepId(1)) == std::vector<StepId>{ StepId(2), StepId(3) });   // in their places
    CHECK(h.current() == StepId(2));   // still C
    h.goTo(StepId(1));
    CHECK(h.redoChild(StepId(1)) == StepId(2));   // redo comes back to C

    h.remove({ kStartStep });   // never Start
    CHECK(h.size() == 4);
    h.goTo(StepId(3));
    h.remove({ StepId(1) });    // A, with both branches under it: they go up to Start
    CHECK(h.children(kStartStep) == std::vector<StepId>{ StepId(1), StepId(2) });
    CHECK(h.step(h.current()).label == "E");
}
