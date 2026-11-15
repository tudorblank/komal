// The graph's rules, apart from its panel: where a dragged link lands on a node (link-drop.hpp)
// and how a stack's rows are picked (row-pick.hpp).

#include "project/workspace.hpp"
#include "ui/panels/graph/internal/link-drop.hpp"
#include "ui/panels/graph/internal/row-pick.hpp"

#include <doctest/doctest.h>

namespace {

// The master over one raster layer, and a Move fed by nothing.
struct Graph {
    Workspace p;
    NodeId layer = kNoNodeId;
    NodeId move  = kNoNodeId;

    Graph()
    {
        p.init([&](Workspace& s) {
            TileGrid g;
            g.writeOrCreateTile(0, 0).setPx(0, 0, RGBA{ 255, 0, 0, 255 });
            layer = s.importTiles(g, "", {});
        });
        move = p.addNode(NodeType::Move, 400.0f, 0.0f, {});
    }

    NodeItem layout(NodeId id) const
    {
        NodeItem item;
        buildNodeItem(p, *p.find(id), {}, {}, item);
        return item;
    }
};

const auto kAll = [](const InputSlot&) { return true; };
const auto kNone = [](const InputSlot&) { return false; };
const StackGap kRoom = StackGap::room(StackGap::Kind::Link, { kNoArtboard, 1 });

// The stack's drop, as stackPlace would give it: a room at the top, and what it was asked to carry.
struct Placer {
    std::vector<StackRowRef> carried;
    bool laidOut = true;   // false: the stack isn't laid out for the drag yet
    StackGap operator()(ImVec2, const std::vector<StackRowRef>& c)
    {
        carried = c;
        return laidOut ? kRoom : StackGap{};
    }
};

}

TEST_CASE("a link over a stack's rows goes into the stack, on top, where the drop is")
{
    const Graph g;
    const NodeItem item = g.layout(g.p.master().id());
    REQUIRE(item.stackArea);
    Placer place;
    const LinkLanding l = linkLanding(item, g.p.master(), item.stackArea->GetCenter(), nullptr, {}, kAll, std::ref(place));
    CHECK(l.valid);
    CHECK(l.slot == InputSlot::newLayer(uint32_t(g.p.master().layers().size())));
    CHECK(l.place == kRoom);
    CHECK(place.carried.empty());

    CHECK_FALSE(linkLanding(item, g.p.master(), item.stackArea->GetCenter(), nullptr, {}, kNone, std::ref(place)).valid);   // refused
    place.laidOut = false;
    CHECK_FALSE(linkLanding(item, g.p.master(), item.stackArea->GetCenter(), nullptr, {}, kAll, std::ref(place)).valid);   // no drop yet
}

TEST_CASE("a link lifted off a stack's own row carries that row, and lands as its bottom layer first")
{
    const Graph g;
    const NodeItem item = g.layout(g.p.master().id());
    REQUIRE(item.stackArea);
    Placer place;
    const LinkEnd lifted{ true, g.layer, g.p.master().id() };
    const LinkLanding l = linkLanding(item, g.p.master(), item.stackArea->GetCenter(), nullptr, lifted, kAll, std::ref(place));
    CHECK(l.valid);
    CHECK(l.slot == InputSlot::newLayer(0));
    CHECK(place.carried == std::vector<StackRowRef>{ { g.layer, kNoArtboard } });
}

TEST_CASE("a link over a channel's row aims at that channel's mask; on a one-input node's body, its input")
{
    const Graph g;
    const NodeItem layer = g.layout(g.layer);
    REQUIRE(!layer.channels.empty());
    const NodeItemChannelRow& row = layer.channels.front();
    Placer place;
    const LinkLanding mask = linkLanding(layer, *g.p.find(g.layer), row.rect.GetCenter(), nullptr, {}, kAll, std::ref(place));
    CHECK(mask.valid);
    CHECK(mask.slot == InputSlot::mask(row.signal, row.channel));

    const NodeItem move = g.layout(g.move);
    REQUIRE(!move.boxes.empty());
    const LinkLanding input = linkLanding(move, *g.p.find(g.move), move.boxes.front().rect.GetCenter(), nullptr, {}, kAll, std::ref(place));
    CHECK(input.valid);
    CHECK(input.slot == InputSlot::input());
    CHECK_FALSE(linkLanding(move, *g.p.find(g.move), ImVec2(-1e6f, -1e6f), nullptr, {}, kAll, std::ref(place)).valid);   // off the node
}

TEST_CASE("a link from an input lands only on an output's port")
{
    const Graph g;
    const NodeItem item = g.layout(g.layer);
    const NodeItemPort* out = item.output();
    REQUIRE(out);
    Placer place;
    const LinkEnd fromInput{ false, kNoNodeId, kNoNodeId };
    CHECK(linkLanding(item, *g.p.find(g.layer), out->pos, out, fromInput, kAll, std::ref(place)).valid);
    CHECK_FALSE(linkLanding(item, *g.p.find(g.layer), out->pos, nullptr, fromInput, kAll, std::ref(place)).valid);   // not on it
    CHECK_FALSE(linkLanding(item, *g.p.find(g.layer), out->pos, out, fromInput, kNone, std::ref(place)).valid);
}

TEST_CASE("rows are picked like files: click, Ctrl toggles, Shift spans from the anchor")
{
    const StackRowRef a{ NodeId(1), kNoArtboard }, b{ NodeId(2), kNoArtboard }, c{ NodeId(3), kNoArtboard }, d{ NodeId(4), kNoArtboard };
    const std::vector<StackRowRef> shown{ a, b, c, d };   // top first

    RowPick p = pickRow(shown, {}, {}, b, false, false);   // a click
    CHECK(p.picked == std::vector{ b });
    CHECK(p.anchor == b);
    CHECK(p.carried == std::vector{ b });
    CHECK_FALSE(p.wasPicked);

    p = pickRow(shown, p.picked, p.anchor, d, true, false);   // Ctrl adds
    CHECK(p.picked == std::vector{ b, d });
    CHECK(p.carried == std::vector{ b, d });   // as shown, top first
    p = pickRow(shown, p.picked, p.anchor, b, true, false);   // Ctrl takes out: nothing to drag
    CHECK(p.picked == std::vector{ d });
    CHECK(p.carried.empty());

    p = pickRow(shown, { d }, d, a, false, true);   // Shift: from the anchor to it
    CHECK(p.picked.size() == 4);
    CHECK(p.carried == shown);
    CHECK(p.anchor == d);   // the anchor stays: the range can change its other end

    p = pickRow(shown, { a }, a, c, true, true);   // Ctrl+Shift adds the range
    CHECK(p.carried == std::vector{ a, b, c });

    p = pickRow(shown, { a, c }, a, c, false, false);   // a plain click on a row of a bigger pick
    CHECK(p.wasPicked);
    CHECK(p.picked == std::vector{ a, c });   // kept: a drag carries them all
}
