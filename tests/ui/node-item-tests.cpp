// How the graph lays a node's controls out (node-item.hpp): a channel's slider and its value as
// widgets of their own, and the rows a drag carries floating where the stack shows its rows.

#include "project/workspace.hpp"
#include "ui/panels/graph/internal/node-item.hpp"
#include "ui/panels/graph/internal/layout.hpp"

#include <doctest/doctest.h>

using namespace GraphLayout;

namespace {

// The master over one raster layer, and an artboard.
struct Graph {
    Workspace p;
    NodeId layer = kNoNodeId;
    ArtboardId board = kNoArtboard;

    Graph()
    {
        p.init([&](Workspace& s) {
            TileGrid g;
            g.writeOrCreateTile(0, 0).setPx(0, 0, RGBA{ 255, 0, 0, 255 });
            layer = s.importTiles(g, "", {});
        });
        board = p.addArtboard({ 0, 0, 9, 9 });
    }

    NodeItem layout(const Node& n) const
    {
        NodeItem item;
        buildNodeItem(p, n, {}, {}, item);
        return item;
    }
};

template <class T>
const NodeItemWidget* widgetFor(const NodeItem& item, const NodeItemChannelRow& row)
{
    for (const NodeItemWidget& w : item.widgets)
        if (const T* c = w.as<T>(); c && c->signal == row.signal && c->channel == row.channel) return &w;
    return nullptr;
}

}

TEST_CASE("a channel's track and its value are widgets of their own, side by side")
{
    const Graph g;
    const NodeItem item = g.layout(*g.p.find(g.layer));
    REQUIRE(!item.channels.empty());
    for (const NodeItemChannelRow& row : item.channels) {
        const NodeItemWidget* track = widgetFor<ScaleControl>(item, row);
        const NodeItemWidget* value = widgetFor<ScaleValueControl>(item, row);
        REQUIRE(track);
        REQUIRE(value);
        CHECK(track->rect.Min.x == row.track.Min.x - kTrackReach);
        CHECK(track->rect.Max.x == row.track.Max.x + kTrackReach);
        CHECK(value->rect.Min.x == track->rect.Max.x);
        CHECK(value->rect.Max.x == row.rect.Max.x - kRowEndInset);
        CHECK(track->rect.Min.y == row.rect.Min.y);
        CHECK(value->rect.Max.y == row.rect.Max.y);
    }
}

TEST_CASE("carried rows float where the stack lays its rows out, their links ending on the node's edge")
{
    const Graph g;
    const NodeItem item = g.layout(g.p.master());
    const NodeItemLayerRow* shown = nullptr;
    for (const NodeItemLayerRow& r : item.layers)
        if (r.as<NodeItemLayerRow::Layer>() && r.inside == kNoArtboard) shown = &r;
    REQUIRE(shown);

    // An artboard over a layer, the layer pressed with its top at 100.
    const std::vector<StackRowRef> rows{ { kNoNodeId, g.board }, { g.layer, kNoArtboard } };
    const std::vector<CarriedRow> carried = carriedRows(item, rows, 1, 100.0f);
    REQUIRE(carried.size() == 2);
    CHECK(carried[1].rect.Min.y == 100.0f);
    CHECK(carried[1].rect.GetHeight() == kLayerRowH);
    CHECK(carried[0].rect.GetHeight() == kArtboardRowH);
    CHECK(carried[0].rect.Max.y + kLayerGap == doctest::Approx(carried[1].rect.Min.y));
    for (const CarriedRow& c : carried) {
        CHECK(c.rect.Min.x == shown->rect.Min.x);
        CHECK(c.rect.Max.x == shown->rect.Max.x);
        CHECK(c.port.x == doctest::Approx(item.header.Min.x));
        CHECK(c.port.y == c.rect.GetCenter().y);
    }
}
