#include "document/history.hpp"

#include <doctest/doctest.h>

TEST_CASE("the history is a tree: a step after undoing starts a branch")
{
    History h;
    std::vector<RasterCommitRef> flipped;
    const auto flip = [&](const RasterCommitRef& c) { flipped.push_back(c); };
    const auto commit = [](uint32_t n) { return RasterCommitRef{ RasterId(0), RasterCommitId(n) }; };

    h.push({ "A", { commit(1) }, nullptr });
    const StepId a = h.current();
    h.push({ "B", { commit(2) }, nullptr });
    const StepId b = h.current();
    CHECK(h.parent(b) == a);

    h.goTo(a, flip);   // undo B
    REQUIRE(flipped.size() == 1);
    CHECK(flipped[0].commit == RasterCommitId(2));
    CHECK(h.redoChild(a) == b);

    h.push({ "C", {}, nullptr });   // a branch: B stays
    const StepId c = h.current();
    CHECK(h.children(a).size() == 2);
    CHECK(h.redoChild(a) == c);

    flipped.clear();
    h.goTo(b, flip);   // across branches: back to A (C has no commits), then B again
    REQUIRE(flipped.size() == 1);
    CHECK(flipped[0].commit == RasterCommitId(2));
    CHECK(h.current() == b);
    CHECK(h.canUndo());
    CHECK_FALSE(h.canRedo());
}
