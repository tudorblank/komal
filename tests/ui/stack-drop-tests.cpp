// Where a drop on a stack lands (stack-drop.hpp: stackDrop, landing), on the master as the graph lays it out: two
// artboards and a layer of its own. The stack is an array of rows (each artboard one, taking its
// space); an open artboard holds its own array. A drop goes between rows (room there) or onto an
// artboard's row (into it, nothing moving).

#include "document/workspace.hpp"
#include "ui/panels/graph/internal/node-item.hpp"
#include "ui/panels/graph/internal/stack-drop.hpp"
#include "ui/panels/graph/internal/layout.hpp"

#include <doctest/doctest.h>
#include <algorithm>

namespace {

using Target = StackGap::Target;
constexpr ArtboardId kTopLevel = kNoArtboard;   // a place in no artboard
constexpr auto kRows = StackGap::Kind::Rows;

// Master, top first: [A: a2 a1] [B: b1] d. The top level, bottom first: d 0, B 1, A 2.
struct Stack {
    Workspace p;
    NodeId a1, a2, b1, d;
    ArtboardId A, B;
    NodeItemState view;

    Stack()
    {
        p.init([&](Workspace& s) {
            const auto raster = [&] {
                TileGrid g;
                g.writeOrCreateTile(0, 0).setPx(0, 0, RGBA{ 255, 0, 0, 255 });
                return s.importTiles(g, "", {});
            };
            d = raster(); b1 = raster(); a1 = raster(); a2 = raster();
        });
        B = p.addArtboard({ 0, 0, 9, 9 });
        A = p.addArtboard({ 0, 0, 9, 9 });
        REQUIRE(p.moveStackRows(p.master().id(), { { b1 }, {}, B, 0 }));
        REQUIRE(p.moveStackRows(p.master().id(), { { a1, a2 }, {}, A, 0 }));
    }

    NodeItem layout(StackGap drag = {}, std::vector<StackRowRef> carried = {}) const
    {
        NodeItem item;
        buildNodeItem(p, p.master(), view, { drag, std::move(carried) }, item);
        return item;
    }
};

const NodeItemLayerRow& rowOf(const Workspace& p, const NodeItem& item, NodeId layer, ArtboardId board = kNoArtboard)
{
    for (const NodeItemLayerRow& r : item.layers) {
        if (const auto* a = r.as<NodeItemLayerRow::Artboard>(); a && board != kNoArtboard && a->key == board) return r;
        if (const auto* l = r.as<NodeItemLayerRow::Layer>(); l && layer != kNoNodeId && p.master().layers()[l->index]->id() == layer) return r;
    }
    FAIL("no such row");
    return item.layers.front();
}

// The drop at y as the panel works it out: on the layout the drag shows now, then settled.
StackGap settled(const Stack& s, const StackGap& now, float y, const std::vector<StackRowRef>& carried = {}, StackGap::Kind kind = StackGap::Kind::Rows)
{
    const StackGap drop = stackDrop(s.layout(now, carried), y, kind, false, carried);
    return settleDrop(drop, [&](const StackGap& g) { return s.layout(g, carried); }, y, kind, false, carried);
}

const NodeItemLayerRow& boardRow(const Stack& s, const NodeItem& item, ArtboardId a) { return rowOf(s.p, item, kNoNodeId, a); }
const NodeItemLayerRow& layerRow(const Stack& s, const NodeItem& item, NodeId l) { return rowOf(s.p, item, l); }
float at(const NodeItemLayerRow& r, float t) { return r.rect.Min.y + r.rect.GetHeight() * t; }

// Where a drop at height y lands, as a place (into, at); and whether it's onto an artboard.
StackPlace lands(const NodeItem& item, float y, bool carriesArtboard = false)
{
    return landing(item, stackDrop(item, y, kRows, carriesArtboard));
}

void check(const StackPlace& p, ArtboardId into, size_t pos)
{
    CHECK(p.into == into);
    CHECK(p.at == pos);
}

}

TEST_CASE("between rows, onto artboards: every place of the stack")
{
    const Stack s;
    const NodeItem item = s.layout();
    const ArtboardId A = s.A, B = s.B;
    check(lands(item, item.layers.front().rect.Min.y - 50.0f), kTopLevel, 3);   // above everything: the top
    check(lands(item, at(boardRow(s, item, s.A), 0.1f)), A, 2);         // A first: all of its row is onto it ...
    check(lands(item, at(boardRow(s, item, s.A), 0.6f)), A, 2);         // ... in it, on top
    CHECK(stackDrop(item, at(boardRow(s, item, s.A), 0.6f), kRows, false).artboard() == A);
    check(lands(item, at(layerRow(s, item, s.a2), 0.3f)), A, 2);        // A's own array: above a2
    check(lands(item, at(layerRow(s, item, s.a2), 0.7f)), A, 1);        // between a2 and a1
    check(lands(item, at(layerRow(s, item, s.a1), 0.7f)), A, 0);        // under a1: in A, last
    check(lands(item, at(boardRow(s, item, s.B), 0.1f)), kTopLevel, 2);         // B's top edge: between A and B, outside both
    check(lands(item, at(boardRow(s, item, s.B), 0.6f)), B, 1);         // onto B
    check(lands(item, at(layerRow(s, item, s.b1), 0.7f)), B, 0);        // in B, last
    check(lands(item, at(layerRow(s, item, s.d), 0.3f)), kTopLevel, 1);         // above d: between B and d
    check(lands(item, at(layerRow(s, item, s.d), 0.7f)), kTopLevel, 0);         // the bottom
    check(lands(item, layerRow(s, item, s.d).rect.Max.y + 200.0f), kTopLevel, 0);
}

TEST_CASE("onto an artboard: no room anywhere, the artboard outlined")
{
    const Stack s;
    const StackGap between = StackGap::room(kRows, { kTopLevel, 2 });   // room between A and B, then onto B
    const NodeItem open = s.layout(between);
    const StackGap onto = stackDrop(open, at(boardRow(s, open, s.B), 0.6f), kRows, false);
    CHECK(onto.artboard() == s.B);
    CHECK_FALSE(onto.opens());
    const NodeItem shown = s.layout(onto);
    CHECK(std::none_of(shown.layers.begin(), shown.layers.end(), [](const NodeItemLayerRow& r) { return r.as<NodeItemLayerRow::Gap>(); }));
    REQUIRE(shown.dropInto.has_value());   // B outlined
    CHECK(shown.dropInto->Min.y == boardRow(s, shown, s.B).rect.Min.y);
}

TEST_CASE("folded and empty artboards take a drop on their row; their layers show no ports")
{
    Stack s;
    s.view.folded = { s.A };
    const NodeItem folded = s.layout();
    check(lands(folded, at(boardRow(s, folded, s.A), 0.6f)), s.A, 2);   // on top of its two layers
    check(lands(folded, at(boardRow(s, folded, s.B), 0.1f)), kTopLevel, 2);               // under it: B's top edge
    for (const NodeItemPort& port : folded.ports) CHECK_FALSE(port.slot == InputSlot::stackLayer(2));
    CHECK(folded.linkEnd(InputSlot::stackLayer(2)).has_value());                  // their links end at its row

    const ArtboardId C = s.p.addArtboard({ 0, 0, 9, 9 });   // empty, moved to the bottom
    REQUIRE(s.p.moveStackRows(s.p.master().id(), { {}, { C }, kNoArtboard, 0 }));
    const NodeItem empty = s.layout();
    check(lands(empty, at(boardRow(s, empty, C), 0.6f)), C, 0);          // onto it ...
    check(lands(empty, at(boardRow(s, empty, C), 0.1f)), C, 0);          // ... all of its row: d above it gives "between"
    check(lands(empty, at(layerRow(s, empty, s.d), 0.7f)), kTopLevel, 1);                  // under d: between d and it
    const StackGap bottom = stackDrop(empty, boardRow(s, empty, C).rect.Max.y + 40.0f, kRows, false);
    check(landing(empty, bottom), kTopLevel, 0);                                           // below it: out, the very bottom ...
    CHECK(bottom.target() == Target::Line);                                              // ... shown as the line
}

TEST_CASE("carrying an artboard: beside the others, never in one")
{
    const Stack s;
    const NodeItem item = s.layout();
    check(lands(item, at(boardRow(s, item, s.B), 0.3f), true), kTopLevel, 2);   // B's upper half: above it
    check(lands(item, at(boardRow(s, item, s.B), 0.7f), true), kTopLevel, 1);   // lower half: under it
    check(lands(item, at(layerRow(s, item, s.a1), 0.9f), true), kTopLevel, 2);  // A's layers: beside A
    check(lands(item, at(layerRow(s, item, s.a2), 0.1f), true), kTopLevel, 3);
}

namespace {

// A drag swept over the stack a step at a time, as the graph does it: each step the drop is worked
// out on the layout the last one left, then laid out again. A drop it settles on must stay put with
// the mouse still; the places it went through are returned.
std::vector<StackPlace> sweep(const Stack& s, float from, float to, StackGap::Kind kind)
{
    std::vector<StackPlace> seen;
    StackGap now{};
    const float step = from < to ? 1.0f : -1.0f;
    for (float y = from; step > 0 ? y <= to : y >= to; y += step) {
        const StackGap next = settled(s, now, y, {}, kind);
        if (next == now) continue;
        now = next;
        CAPTURE(y);
        CHECK_MESSAGE(stackDrop(s.layout(now), y, kind, false) == now, "a drop shown isn't the one the mouse is on");
        const StackPlace p = landing(s.layout(now), now);
        if (std::find(seen.begin(), seen.end(), p) == seen.end()) seen.push_back(p);
    }
    return seen;
}

bool reached(const std::vector<StackPlace>& seen, ArtboardId into, size_t pos)
{
    return std::find(seen.begin(), seen.end(), StackPlace{ into, pos }) != seen.end();
}

}

TEST_CASE("a drag swept down and up never jumps, and reaches every place")
{
    // [A: a2 a1] [B: b1] d [C: empty], C last; again with A folded.
    for (const bool foldA : { false, true }) {
        Stack s;
        const ArtboardId C = s.p.addArtboard({ 0, 0, 9, 9 });
        REQUIRE(s.p.moveStackRows(s.p.master().id(), { {}, { C }, kNoArtboard, 0 }));
        if (foldA) s.view.folded = { s.A };
        const NodeItem item = s.layout();
        const float top = item.layers.front().rect.Min.y - 30.0f, bottom = item.layers.back().rect.Max.y + 60.0f;
        for (const StackGap::Kind kind : { StackGap::Kind::Rows, StackGap::Kind::Link }) {
            CAPTURE(foldA);
            // Every place, coming down, and again going up: above each artboard and onto it too.
            for (const std::vector<StackPlace>& seen : { sweep(s, top, bottom, kind), sweep(s, bottom, top, kind) }) {
                // The top level, bottom first: C 0, d 1, B 2, A 3.
                CHECK(reached(seen, kTopLevel, 4));                // the top
                CHECK(reached(seen, kTopLevel, 3));                // between A and B, outside both
                CHECK(reached(seen, kTopLevel, 2));                // between B and d
                CHECK(reached(seen, kTopLevel, 1));                // between d and C
                CHECK(reached(seen, kTopLevel, 0));                // the very bottom, under C
                CHECK(reached(seen, s.A, 2));    // onto A
                CHECK(reached(seen, s.B, 1));    // onto B
                CHECK(reached(seen, s.B, 0));    // in B, last
                CHECK(reached(seen, C, 0));      // onto the empty C
                if (!foldA) {
                    CHECK(reached(seen, s.A, 1));   // between A's layers
                    CHECK(reached(seen, s.A, 0));   // in A, last
                }
            }
        }
    }
}

TEST_CASE("an empty artboard with a layer under it: dragged up, it takes the drop for a while")
{
    // ... d [C: empty] e: e (on its own, under C) dragged up into C. Onto C must last, a row's
    // height at least, before the drop goes above it.
    Stack s;
    const ArtboardId C = s.p.addArtboard({ 0, 0, 9, 9 });
    REQUIRE(s.p.moveStackRows(s.p.master().id(), { {}, { C }, kNoArtboard, 0 }));
    const NodeItem item = s.layout();
    const NodeItemLayerRow& c = boardRow(s, item, C);
    float first = 0.0f, last = 0.0f;
    StackGap now = StackGap::room(kRows, { kTopLevel, 0 });   // e's room under C, as the drag starts
    for (float y = c.rect.Max.y + 40.0f; y >= c.rect.Min.y - 60.0f; y -= 1.0f) {
        now = stackDrop(s.layout(now), y, kRows, false);
        if (now.artboard() == C) {
            if (first == 0.0f) first = y;
            last = y;
        }
    }
    CHECK(first - last >= c.rect.GetHeight());   // onto C over its whole row, and a bit past it
}

TEST_CASE("room is shorter than any row: closing it never moves the row under the mouse away")
{
    // Room above an artboard (from the row above's lower half), then the mouse goes down onto the
    // artboard: the room closes, the artboard comes up by less than its row, it's still under the mouse.
    Stack s;
    const ArtboardId C = s.p.addArtboard({ 0, 0, 9, 9 });
    REQUIRE(s.p.moveStackRows(s.p.master().id(), { {}, { C }, kNoArtboard, 0 }));   // d [C]
    const NodeItem still = s.layout();
    CHECK(GraphLayout::kRoomH + GraphLayout::kLayerGap < boardRow(s, still, C).rect.GetHeight());
    const StackGap aboveC = settled(s, {}, at(layerRow(s, still, s.d), 0.8f));
    check(landing(s.layout(aboveC), aboveC), kTopLevel, 1);   // between d and C: room
    CHECK(aboveC.opens());
    const NodeItem open = s.layout(aboveC);
    for (const float t : { 0.1f, 0.5f, 0.95f }) {   // anywhere on C's row, as it shows with the room
        const StackGap onto = settled(s, aboveC, at(boardRow(s, open, C), t));
        CAPTURE(t);
        CHECK(onto.artboard() == C);
    }
}

TEST_CASE("a drag starting moves nothing: the row it carries keeps its spot")
{
    const Stack s;
    const NodeItem still = s.layout();
    const std::vector<StackRowRef> rows = { { s.a2, kNoArtboard }, { s.a1, kNoArtboard }, { s.b1, kNoArtboard }, { s.d, kNoArtboard }, { kNoNodeId, s.B } };
    for (const StackRowRef& r : rows) {
        const NodeItem carrying = s.layout({}, { r });
        REQUIRE(carrying.layers.size() == still.layers.size());
        for (size_t i = 0; i < still.layers.size(); ++i) {
            CHECK(carrying.layers[i].rect.Min.y == still.layers[i].rect.Min.y);   // nothing closed up
            CHECK(carrying.layers[i].rect.Max.y <= still.layers[i].rect.Max.y);
        }
        size_t spots = 0;
        for (const NodeItemLayerRow& row : carrying.layers) spots += row.as<NodeItemLayerRow::Origin>() != nullptr;
        CHECK(spots == (r.artboard != kNoArtboard ? 2u : 1u));   // its spot, outlined (an artboard's: its row and its layer's)
        REQUIRE(carrying.origin.has_value());  // and where it was
    }
    // Over its own spot: back home, nothing opens; dropped, it changes nothing.
    const NodeItem carrying = s.layout({}, { { s.d, kNoArtboard } });
    const auto spot = std::find_if(carrying.layers.begin(), carrying.layers.end(), [](const NodeItemLayerRow& r) { return r.as<NodeItemLayerRow::Origin>(); });
    REQUIRE(spot != carrying.layers.end());
    const StackGap home = stackDrop(carrying, at(*spot, 0.5f), kRows, false, { { s.d, kNoArtboard } });
    CHECK_FALSE(home.opens());
    CHECK(landing(carrying, home) == *carrying.origin);
    CHECK(landing(carrying, home) == StackPlace{ kTopLevel, 0 });   // d: the bottom of what's left
}

TEST_CASE("a layer right under an empty artboard, dragged up into it: the artboard never moves")
{
    // [A: a2 a1] [B: b1] [C: empty] d; d dragged up into C.
    Stack s;
    const ArtboardId C = s.p.addArtboard({ 0, 0, 9, 9 });
    REQUIRE(s.p.moveStackRows(s.p.master().id(), { {}, { C }, kNoArtboard, 1 }));   // over d
    const std::vector<StackRowRef> carried = { { s.d, kNoArtboard } };
    const auto rowY = [&](const NodeItem& item) {
        for (const NodeItemLayerRow& r : item.layers)
            if (const auto* a = r.as<NodeItemLayerRow::Artboard>(); a && a->key == C) return r.rect.Min.y;
        return -1.0f;
    };
    const NodeItem start = s.layout({}, carried);
    const float startY = rowY(start);
    const auto spot = std::find_if(start.layers.begin(), start.layers.end(), [](const NodeItemLayerRow& r) { return r.as<NodeItemLayerRow::Origin>(); });
    REQUIRE(spot != start.layers.end());
    StackGap now{};
    bool onto = false;
    for (float y = spot->rect.GetCenter().y; y >= rowY(start) - 10.0f && !onto; y -= 1.0f) {
        const StackGap next = settled(s, now, y, carried);
        CAPTURE(y);
        CHECK(rowY(s.layout(next, carried)) == startY);   // never pushed, never pulled
        CHECK(stackDrop(s.layout(next, carried), y, kRows, false, carried) == next);   // and nothing jumps
        now = next;
        onto = now.artboard() == C;
    }
    CHECK(onto);
}

TEST_CASE("carrying a layer, a sweep down and up never jumps, and reaches every place")
{
    // [A: a2 a1] [B: b1] d [C: empty], b1 carried (from inside B).
    Stack s;
    const ArtboardId C = s.p.addArtboard({ 0, 0, 9, 9 });
    REQUIRE(s.p.moveStackRows(s.p.master().id(), { {}, { C }, kNoArtboard, 0 }));
    const std::vector<StackRowRef> carried = { { s.b1, kNoArtboard } };
    const NodeItem item = s.layout({}, carried);
    const float top = item.layers.front().rect.Min.y - 30.0f, bottom = item.layers.back().rect.Max.y + 60.0f;
    std::vector<StackPlace> seen;
    for (const bool down : { true, false }) {
        StackGap now{};
        for (float y = down ? top : bottom; down ? y <= bottom : y >= top; y += down ? 1.0f : -1.0f) {
            const StackGap next = settled(s, now, y, carried);
            if (next == now) continue;
            now = next;
            CAPTURE(down);
            CAPTURE(y);
            CHECK(stackDrop(s.layout(now, carried), y, kRows, false, carried) == now);
            const StackPlace p = landing(s.layout(now, carried), now);
            if (std::find(seen.begin(), seen.end(), p) == seen.end()) seen.push_back(p);
        }
    }
    {   // every place, coming down or going up
        // The top level, bottom first: C 0, d 1, B 2, A 3; B empty now that b1 is carried.
        const auto reached = [&](ArtboardId into, size_t pos) { return std::find(seen.begin(), seen.end(), StackPlace{ into, pos }) != seen.end(); };
        CHECK(reached(kTopLevel, 4));
        CHECK(reached(kTopLevel, 3));
        CHECK(reached(kTopLevel, 2));
        CHECK(reached(kTopLevel, 1));
        CHECK(reached(kTopLevel, 0));
        CHECK(reached(s.A, 2));
        CHECK(reached(s.A, 1));
        CHECK(reached(s.A, 0));
        CHECK(reached(s.B, 0));   // back where it was, or onto B: the same place
        CHECK(reached(C, 0));
    }
}


TEST_CASE("a drop is only worked out on the stack laid out for the drag: the layer's spot kept")
{
    const Stack s;
    const std::vector<StackRowRef> carried = { { s.d, kNoArtboard } };
    const NodeItem stale = s.layout();   // the frame the drag lifts d: not laid out for it yet
    CHECK(stackDrop(stale, at(layerRow(s, stale, s.d), 0.3f), kRows, false, carried).kind() == StackGap::Kind::None);   // nothing yet
    const NodeItem laid = s.layout({}, carried);
    const auto spot = std::find_if(laid.layers.begin(), laid.layers.end(), [](const NodeItemLayerRow& r) { return r.as<NodeItemLayerRow::Origin>(); });
    REQUIRE(spot != laid.layers.end());
    const StackGap first = stackDrop(laid, at(*spot, 0.3f), kRows, false, carried);
    CHECK(first.target() == Target::Home);         // its own spot: back home ...
    CHECK_FALSE(first.opens());   // ... no room anywhere
}

TEST_CASE("an empty artboard with the layer right under it, dragged up onto it: no room ever opens")
{
    // The ref picture: [Artboard 1: empty] Raster 1; Raster 1 dragged up onto the artboard. Nothing on
    // the way is a place between rows (its own spot, onto the artboard): no room, nothing moves.
    // Above the artboard (genuinely above it) the room opens, at the top.
    Workspace p;
    NodeId raster = kNoNodeId;
    p.init([&](Workspace& s) {
        TileGrid g;
        g.writeOrCreateTile(0, 0).setPx(0, 0, RGBA{ 255, 0, 0, 255 });
        raster = s.importTiles(g, "", {});
    });
    const ArtboardId A = p.addArtboard({ 0, 0, 9, 9 });   // on top, empty
    const NodeItemState view;
    const std::vector<StackRowRef> carried = { { raster, kNoArtboard } };
    const auto layout = [&](const StackGap& g) {
        NodeItem item;
        buildNodeItem(p, p.master(), view, { g, carried }, item);
        return item;
    };
    const auto drop = [&](const StackGap& now, float y) {
        return settleDrop(stackDrop(layout(now), y, kRows, false, carried), layout, y, kRows, false, carried);
    };
    const NodeItem start = layout({});
    const NodeItemLayerRow& board = start.layers.front();
    REQUIRE(board.as<NodeItemLayerRow::Artboard>());
    const auto rowsY = [](const NodeItem& item) {
        std::vector<float> ys;
        for (const NodeItemLayerRow& r : item.layers) ys.push_back(r.rect.Min.y);
        return ys;
    };
    StackGap now{};
    bool onto = false;
    for (float y = start.layers.back().rect.GetCenter().y; y >= board.rect.Min.y; y -= 1.0f) {
        now = drop(now, y);
        CAPTURE(y);
        CHECK_FALSE(now.opens());                       // no room ...
        CHECK(rowsY(layout(now)) == rowsY(start));   // ... nothing moves
        onto |= now.artboard() == A;
    }
    CHECK(onto);
    const StackGap above = drop(now, board.rect.Min.y - 30.0f);   // over it: genuinely above it
    CHECK(above.opens());
    check(landing(layout(above), above), kTopLevel, 1);   // the top
}

TEST_CASE("going up, above an artboard opens room under the row above it")
{
    // [A: a2 a1] [B: b1] d [C: empty]: from below C up into d's lower half, and up onto B's top edge.
    Stack s;
    const ArtboardId C = s.p.addArtboard({ 0, 0, 9, 9 });
    REQUIRE(s.p.moveStackRows(s.p.master().id(), { {}, { C }, kNoArtboard, 0 }));
    const NodeItem still = s.layout();
    StackGap now{};
    std::vector<StackPlace> seen;
    for (float y = still.layers.back().rect.Max.y + 20.0f; y >= still.layers.front().rect.Min.y; y -= 1.0f) {
        now = settled(s, now, y);
        if (now.opens()) seen.push_back(landing(s.layout(now), now));
    }
    CHECK(std::find(seen.begin(), seen.end(), StackPlace{ kTopLevel, 1 }) != seen.end());   // between d and C
    CHECK(std::find(seen.begin(), seen.end(), StackPlace{ kTopLevel, 3 }) != seen.end());   // between A and B
}

TEST_CASE("an empty stack: a drop lands on its zone, and nothing changes size")
{
    Workspace p;
    p.init();   // an empty master ...
    const NodeId comp = p.addNode(NodeType::Compositor, 0.0f, 0.0f, {});   // ... and an empty compositor
    const NodeItemState view;
    for (const NodeId id : { p.master().id(), comp }) {
        const auto layout = [&](const StackGap& g) {
            NodeItem item;
            buildNodeItem(p, *p.find(id), view, { g, {} }, item);
            return item;
        };
        const NodeItem still = layout({});
        REQUIRE(still.stackArea.has_value());
        REQUIRE(still.layers.size() == 1);
        REQUIRE(still.layers.front().as<NodeItemLayerRow::Empty>());
        const ImRect zone = still.layers.front().rect;
        for (const StackGap::Kind kind : { StackGap::Kind::Link, StackGap::Kind::Rows })
            for (const float y : { zone.Min.y + 1.0f, zone.GetCenter().y, zone.Max.y - 1.0f, zone.Max.y + 8.0f }) {
                const StackGap drop = settleDrop(stackDrop(still, y, kind, false), layout, y, kind, false, {});
                CAPTURE(y);
                CHECK(drop.opens());
                CHECK(landing(layout(drop), drop) == StackPlace{ kTopLevel, 0 });
                const NodeItem shown = layout(drop);
                REQUIRE(shown.layers.size() == 1);                                   // the zone itself ...
                CHECK(shown.layers.front().as<NodeItemLayerRow::Empty>()->target);   // ... is the target
                CHECK(shown.layers.front().rect.Min.y == zone.Min.y);                // nothing changes size
                CHECK(shown.layers.front().rect.Max.y == zone.Max.y);
                CHECK(shown.stackArea->Max.y == still.stackArea->Max.y);
                CHECK(stackDrop(shown, y, kind, false) == drop);                     // and it holds
                if (kind == StackGap::Kind::Link)                                    // a link lands on its port
                    CHECK(std::any_of(shown.ports.begin(), shown.ports.end(), [](const NodeItemPort& port) { return port.placeholder; }));
            }
    }
}

TEST_CASE("a layer carried down past an empty or folded artboard reaches the place right under it")
{
    // Top first: l1 [C] l2; l1 carried down to between C and l2 ([C] l1 l2). Nothing closed above C,
    // so nothing holds the drop on C past its row: l2's upper half is above l2, as anywhere else.
    for (const bool foldedWithLayer : { false, true }) {
        CAPTURE(foldedWithLayer);
        Workspace p;
        NodeId l1 = kNoNodeId, l2 = kNoNodeId, inside = kNoNodeId;
        p.init([&](Workspace& s) {
            const auto raster = [&] {
                TileGrid g;
                g.writeOrCreateTile(0, 0).setPx(0, 0, RGBA{ 255, 0, 0, 255 });
                return s.importTiles(g, "", {});
            };
            l2 = raster(); inside = raster(); l1 = raster();
        });
        if (!foldedWithLayer) REQUIRE(p.deleteNodes({ inside }));
        const ArtboardId C = p.addArtboard({ 0, 0, 9, 9 });   // on top
        if (foldedWithLayer) REQUIRE(p.moveStackRows(p.master().id(), { { inside }, {}, C, 0 }));
        REQUIRE(p.moveStackRows(p.master().id(), { { l1 }, {}, kNoArtboard, 2 }));   // l1 over it
        NodeItemState view;
        if (foldedWithLayer) view.folded = { C };
        const std::vector<StackRowRef> carried = { { l1, kNoArtboard } };
        const auto layout = [&](const StackGap& g) {
            NodeItem item;
            buildNodeItem(p, p.master(), view, { g, carried }, item);
            return item;
        };
        const NodeItem start = layout({});
        const auto spot = std::find_if(start.layers.begin(), start.layers.end(), [](const NodeItemLayerRow& r) { return r.as<NodeItemLayerRow::Origin>(); });
        REQUIRE(spot != start.layers.end());
        StackGap now{};
        float span = 0.0f;   // how far the mouse goes with the drop right under C
        for (float y = spot->rect.GetCenter().y; y <= start.layers.back().rect.Max.y + 20.0f; y += 1.0f) {
            now = settleDrop(stackDrop(layout(now), y, kRows, false, carried), layout, y, kRows, false, carried);
            CAPTURE(y);
            CHECK(stackDrop(layout(now), y, kRows, false, carried) == now);   // nothing jumps
            if (landing(layout(now), now) == StackPlace{ kTopLevel, 1 }) span += 1.0f;
        }
        CHECK(span >= GraphLayout::kLayerRowH * 0.4f);   // most of l2's upper half, not a pixel or two
    }
}
