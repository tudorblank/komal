// A stack's rows: layers muted in their compositor, rows moved together, and the master's
// artboards (a group of its stack each, with a fill behind its layers).

#include "helpers.hpp"
#include "document/document-actions.hpp"
#include "document/document.hpp"

namespace {

NodeId pixelAt(Workspace& p, int x, int y, RGBA c)
{
    return rasterWith(p, [&](TileGrid& g) { fillPixel(g, x, y, c); });
}

std::vector<NodeId> layerIds(const Workspace& p, NodeId compositor)
{
    std::vector<NodeId> ids;
    for (const NodePtr& l : p.find(compositor)->links().layers) ids.push_back(l->id());
    return ids;
}

const CompositorSettings& stackOf(const Workspace& p, NodeId compositor)
{
    return static_cast<const CompositorNode*>(p.find(compositor))->stack();
}

constexpr RGBA kRed{ 255, 0, 0, 255 }, kBlue{ 0, 0, 255, 255 };
constexpr ArtboardId kFirst{ 1 };   // a project's first artboard: its group's key

}

TEST_CASE("a muted layer is left out of its compositor, and only there")
{
    StarterWorkspace s;
    Workspace& p = s.workspace;
    const NodeId red  = pixelAt(p, 0, 0, kRed);
    const NodeId blue = pixelAt(p, 0, 0, kBlue);
    const NodeId comp = p.addNode(NodeType::Compositor, 0, 0, {});
    REQUIRE(p.connect(red, comp, InputSlot::newLayer(0)));
    REQUIRE(p.connect(blue, comp, InputSlot::newLayer(1)));   // on top
    CHECK(rendered(p, comp, 0, 0) == kBlue);

    REQUIRE(p.setLayerMuted(comp, blue, true));
    CHECK(rendered(p, comp, 0, 0) == kRed);
    CHECK(rendered(p, blue, 0, 0) == kBlue);   // the layer itself still shows
    CHECK_FALSE(p.setLayerMuted(comp, blue, true));   // already
    REQUIRE(p.setLayerMuted(comp, red, true));
    CHECK(rendered(p, comp, 0, 0) == transparent());
    CHECK(p.find(comp)->bounds().empty());
    CHECK_FALSE(p.setLayerMuted(comp, s.paint, true));   // not one of its layers

    // A layer that leaves takes its mute along: linked again, it shows.
    REQUIRE(p.disconnect(comp, InputSlot::stackLayer(1)));
    CHECK(stackOf(p, comp).muted == std::vector<NodeId>{ red });
    REQUIRE(p.connect(blue, comp, InputSlot::newLayer(1)));
    CHECK(rendered(p, comp, 0, 0) == kBlue);
}

TEST_CASE("muting a master layer reaches the master's cached composite, and undoes")
{
    StarterWorkspace s;
    Workspace& p = s.workspace;
    const NodeId master = s.master;
    CHECK(rendered(p, master, -48, -48) == RGBA{ 255, 160, 40, 255 });   // the disc
    step(p, "Mute", [&] { REQUIRE(p.setLayerMuted(master, s.paint, true)); });
    CHECK(rendered(p, master, -48, -48) == transparent());
    REQUIRE(p.undo());
    CHECK(rendered(p, master, -48, -48) == RGBA{ 255, 160, 40, 255 });
    REQUIRE(p.redo());
    CHECK(rendered(p, master, -48, -48) == transparent());
}

TEST_CASE("a stack's top level: each group where it begins, the layers between")
{
    using StackGroups::Entry;
    constexpr GroupKey g{ 7 }, h{ 8 };
    // rows 0 1 2 3 4: [g: 1 2], h empty at 4, under row 4
    const std::vector<StackGroup> groups{ { g, 1, 3 }, { h, 4, 4 } };
    CHECK(StackGroups::entries(groups, 5) == std::vector<Entry>{ { kNoGroup, 0, 1 }, { g, 1, 3 }, { kNoGroup, 3, 4 }, { h, 4, 4 }, { kNoGroup, 4, 5 } });
    CHECK(StackGroups::entries({ { g, 0, 0 } }, 0) == std::vector<Entry>{ { g, 0, 0 } });   // an empty group in an empty stack
    CHECK(StackGroups::entries({}, 2) == std::vector<Entry>{ { kNoGroup, 0, 1 }, { kNoGroup, 1, 2 } });
}

TEST_CASE("groups as a stack holds them: keyed, once each, in range, apart")
{
    constexpr GroupKey g{ 7 }, h{ 8 };
    CHECK(StackGroups::normalized({ { g, 1, 3 }, { g, 3, 4 } }, 5) == std::vector<StackGroup>{ { g, 1, 3 } });   // each key once
    CHECK(StackGroups::normalized({ { kNoGroup, 0, 2 }, { h, 0, 1 } }, 5) == std::vector<StackGroup>{ { h, 0, 1 } });   // none without a key
    CHECK(StackGroups::normalized({ { g, 1, 9 } }, 3) == std::vector<StackGroup>{ { g, 1, 3 } });                  // in range
    CHECK(StackGroups::normalized({ { g, 0, 3 }, { h, 1, 2 } }, 5) == std::vector<StackGroup>{ { g, 0, 3 }, { h, 3, 3 } });   // apart
}

TEST_CASE("groups follow rows: in strictly inside joins, at an edge stays out, a move keeps its group at the edges")
{
    constexpr GroupKey g{ 7 };
    std::vector<StackGroup> groups{ { g, 1, 3 } };
    StackGroups::rowInserted(groups, 2);   // inside
    CHECK(groups == std::vector<StackGroup>{ { g, 1, 4 } });
    StackGroups::rowInserted(groups, 1);   // at its bottom edge: under it
    CHECK(groups == std::vector<StackGroup>{ { g, 2, 5 } });
    StackGroups::rowRemoved(groups, 0);
    CHECK(groups == std::vector<StackGroup>{ { g, 1, 4 } });
    StackGroups::rowMoved(groups, 1, 3);   // its bottom row to its top: still in it
    CHECK(groups == std::vector<StackGroup>{ { g, 1, 4 } });
    StackGroups::rowMoved(groups, 0, 4);   // a row from outside to past its top edge: stays out
    CHECK(groups == std::vector<StackGroup>{ { g, 0, 3 } });
    StackGroups::rowsCleared(groups);
    CHECK(groups == std::vector<StackGroup>{ { g, 0, 0 } });
}

TEST_CASE("moved rows keep their order and close up")
{
    const NodeId a{ 1 }, b{ 2 }, c{ 3 }, d{ 4 };
    constexpr GroupKey g{ 7 };
    const StackRows now{ { a, b, c, d }, {} };
    CHECK(moveRows(now, { { a, c }, {}, kNoGroup, 2 })->layers == std::vector{ b, d, a, c });   // to the top
    CHECK(moveRows(now, { { a, c }, {}, kNoGroup, 1 })->layers == std::vector{ b, a, c, d });   // above b
    CHECK(moveRows(now, { { c, a }, {}, kNoGroup, 0 })->layers == std::vector{ a, c, b, d });   // the stack's order, not the list's
    CHECK_FALSE(moveRows(now, { { a }, {}, kNoGroup, 4 }));        // past the top of what's left
    CHECK_FALSE(moveRows(now, { { NodeId{ 9 } }, {}, kNoGroup, 0 }));
    CHECK_FALSE(moveRows(now, { {}, {}, kNoGroup, 0 }));           // nothing moved

    // With groups: [a] [g: b c] [d]. Layers go in and out; a group moves whole, never into another.
    const StackRows grouped{ { a, b, c, d }, { { g, 1, 3 } } };
    const StackRows in = *moveRows(grouped, { { a }, {}, g, 2 });
    CHECK(in.layers == std::vector{ b, c, a, d });
    CHECK(in.groups == std::vector<StackGroup>{ { g, 0, 3 } });
    const StackRows out = *moveRows(grouped, { { b }, {}, kNoGroup, 3 });   // [a] [g: c] [d] [b]
    CHECK(out.layers == std::vector{ a, c, d, b });
    CHECK(out.groups == std::vector<StackGroup>{ { g, 1, 2 } });
    const StackRows whole = *moveRows(grouped, { {}, { g }, kNoGroup, 2 });   // to the top, its layers along
    CHECK(whole.layers == std::vector{ a, d, b, c });
    CHECK(whole.groups == std::vector<StackGroup>{ { g, 2, 4 } });
    CHECK_FALSE(moveRows(grouped, { {}, { g }, g, 0 }));   // a group into a group
    const StackRows empty{ { a, b }, { { g, 1, 1 } } };     // an empty group between a and b
    CHECK(moveRows(empty, { { a }, {}, g, 0 })->groups == std::vector<StackGroup>{ { g, 0, 1 } });
}

TEST_CASE("a stack's groups follow its rows through every link edit")
{
    StarterWorkspace s;
    Workspace& p = s.workspace;
    const NodeId master = s.master;   // [background, paint]
    const ArtboardId board = p.addArtboard({ 0, 0, 99, 99 });
    REQUIRE(board != kNoArtboard);
    CHECK(stackOf(p, master).groups == std::vector<StackGroup>{ { kFirst, 2, 2 } });   // empty, on top
    REQUIRE(p.moveStackRows(master, { { s.paint }, {}, board, 0 }));      // [background] [board: paint]
    CHECK(stackOf(p, master).groups == std::vector<StackGroup>{ { kFirst, 1, 2 } });

    const NodeId x = pixelAt(p, 0, 0, kRed), y = pixelAt(p, 0, 0, kBlue);
    REQUIRE(p.connect(x, master, InputSlot::newLayer(1)));   // at the board's bottom edge: outside, under it
    CHECK(stackOf(p, master).groups == std::vector<StackGroup>{ { kFirst, 2, 3 } });
    REQUIRE(p.connect(y, master, InputSlot::newLayer(3)));   // at its top edge: outside, over it
    CHECK(stackOf(p, master).groups == std::vector<StackGroup>{ { kFirst, 2, 3 } });
    REQUIRE(p.moveStackRows(master, { { y }, {}, board, 1 }));   // [background x] [board: paint y]
    CHECK(stackOf(p, master).groups == std::vector<StackGroup>{ { kFirst, 2, 4 } });
    REQUIRE(p.moveStackRows(master, { { y }, {}, board, 0 }));   // y under paint: still in it
    CHECK(stackOf(p, master).groups == std::vector<StackGroup>{ { kFirst, 2, 4 } });
    REQUIRE(p.deleteNodes({ s.paint }));  // a layer that goes leaves it
    CHECK(stackOf(p, master).groups == std::vector<StackGroup>{ { kFirst, 2, 3 } });
    CHECK(layerIds(p, master) == std::vector{ s.background, x, y });
    CHECK_FALSE(p.setNodeSettings(master, CompositorSettings{ {}, {} }));   // groups are the artboards', not settings
}

TEST_CASE("an artboard fills its rect behind its layers, which still show past its edges")
{
    StarterWorkspace s;
    Workspace& p = s.workspace;
    const NodeId master = s.master;
    const NodeId red = pixelAt(p, 1200, 5, kRed);   // outside the board's rect (and the starter's pixels)
    REQUIRE(p.connect(red, master, InputSlot::newLayer(2)));
    const ArtboardId id = p.addArtboard({ 1000, 0, 1049, 49 });
    REQUIRE(p.moveStackRows(master, { { red }, {}, id, 0 }));
    CHECK(rendered(p, master, 1020, 10) == transparent());   // a new one is transparent: nothing yet

    Artboard a = *p.artboard(id);
    CHECK(a.name == "Artboard 1");
    a.color = Color8{ 0, 255, 0, 255 };
    step(p, "Color", [&] { REQUIRE(p.setArtboard(a)); });
    CHECK(rendered(p, master, 1020, 10) == RGBA{ 0, 255, 0, 255 });
    CHECK(rendered(p, master, 999, 10) == transparent());       // just outside it
    CHECK(rendered(p, master, 1200, 5) == kRed);                 // its layer, past its edge
    CHECK(p.find(master)->bounds().maxX >= 1200);
    CHECK(p.find(master)->bounds().contains(1000, 0));

    a.visible = false;   // its eye: the fill and its layers go
    REQUIRE(p.setArtboard(a));
    CHECK(rendered(p, master, 1020, 10) == transparent());
    CHECK(rendered(p, master, 1200, 5) == transparent());

    REQUIRE(p.undo());   // back to before the colour (the eye wasn't a step of its own)
    CHECK(p.artboard(id)->color.a == 0);
    CHECK(rendered(p, master, 1020, 10) == transparent());
    CHECK(rendered(p, master, 1200, 5) == kRed);
    REQUIRE(p.redo());
    CHECK(rendered(p, master, 1020, 10) == RGBA{ 0, 255, 0, 255 });

    step(p, "Delete", [&] { REQUIRE(p.deleteArtboard(id)); });   // its layer stays, in no artboard
    CHECK_FALSE(p.artboard(id));
    CHECK(stackOf(p, master).groups.empty());
    CHECK(rendered(p, master, 1020, 10) == transparent());
    CHECK(rendered(p, master, 1200, 5) == kRed);
    REQUIRE(p.undo());
    REQUIRE(p.artboard(id));
    CHECK(stackOf(p, master).groups.size() == 1);
    CHECK(rendered(p, master, 1020, 10) == RGBA{ 0, 255, 0, 255 });
}

TEST_CASE("artboards refuse what doesn't fit")
{
    StarterWorkspace s;
    Workspace& p = s.workspace;
    CHECK(p.addArtboard({}) == kNoArtboard);
    CHECK(p.addArtboard({ 0, 0, TileUtils::kWorldLimit + 10, 5 }) == kNoArtboard);
    const ArtboardId id = p.addArtboard({ 0, 0, 9, 9 });
    Artboard a = *p.artboard(id);
    CHECK_FALSE(p.setArtboard(a));   // nothing changes
    a.name.clear();
    CHECK_FALSE(p.setArtboard(a));
    Artboard ghost = *p.artboard(id);
    ghost.id = ArtboardId{ 99 };
    CHECK_FALSE(p.setArtboard(ghost));
    CHECK_FALSE(p.deleteArtboard(ArtboardId{ 99 }));
    const NodeId comp = p.addNode(NodeType::Compositor, 0, 0, {});
    REQUIRE(p.connect(s.paint, comp, InputSlot::newLayer(0)));
    CHECK_FALSE(p.moveStackRows(comp, { { s.paint }, {}, id, 0 }));   // only the master has artboards
}

TEST_CASE("the stack's actions: mute, move rows, artboards, each one undo step")
{
    Document document;
    Workspace& p = document.workspace();
    NodeId a = kNoNodeId, b = kNoNodeId;
    p.init([&](Workspace& seeded) {
        TileGrid one, two;
        fillPixel(one, 0, 0, kRed);
        fillPixel(two, 0, 0, kBlue);
        a = seeded.importTiles(one, "", {});
        b = seeded.importTiles(two, "", {});   // [a b]
    });
    ActionRegistry& actions = document.actions();
    const NodeId master = p.master().id();
    using namespace DocArgs;

    REQUIRE(actions.run(Actions::LayerMute, { { kNode, master }, { kLayer, b }, { kOn, true } }));
    CHECK(rendered(p, master, 0, 0) == kRed);
    const ActionResult made = actions.run(Actions::ArtboardAdd, { { kMinX, 0 }, { kMinY, 0 }, { kMaxX, 9 }, { kMaxY, 9 } });
    REQUIRE(made);
    const ArtboardId board{ uint32_t(made.made) };
    REQUIRE(actions.run(Actions::LayersMove, { { kNode, master }, { kLayers, std::vector<NodeId>{ a, b } }, { kIntoArtboard, board },
                                               { kLayerTo, size_t(0) } }));
    CHECK(stackOf(p, master).groups == std::vector<StackGroup>{ { board, 0, 2 } });
    REQUIRE(actions.run(Actions::ArtboardSet, { { kArtboard, board }, { kColor, Color8{ 0, 255, 0, 255 } }, { kName, std::string("Cover") } }));
    CHECK(p.artboard(board)->name == "Cover");
    CHECK(rendered(p, master, 5, 5) == RGBA{ 0, 255, 0, 255 });
    CHECK(rendered(p, master, 0, 0) == kRed);   // its layer over its fill
    CHECK_FALSE(actions.run(Actions::LayersMove, { { kNode, master }, { kLayers, std::vector<NodeId>{} },
                                                   { kArtboards, std::vector<ArtboardId>{ board } }, { kLayerTo, size_t(0) } }));   // already there

    // Undone one by one, back to the start.
    for (int i = 0; i < 4; ++i) REQUIRE(actions.run(Actions::EditUndo));
    CHECK(p.artboards().empty());
    CHECK(stackOf(p, master).groups.empty());
    CHECK(stackOf(p, master).muted.empty());
    CHECK(rendered(p, master, 0, 0) == kBlue);
    CHECK_FALSE(actions.run(Actions::EditUndo));
}

TEST_CASE("a link dropped on a stack lands at a place: into an artboard, at its top or its bottom")
{
    StarterWorkspace s;
    Workspace& p = s.workspace;
    const NodeId master = s.master;   // [background, paint]
    const ArtboardId id = p.addArtboard({ 0, 0, 9, 9 });
    REQUIRE(p.moveStackRows(master, { { s.paint }, {}, id, 0 }));   // [background] [board: paint]
    const NodeId x = pixelAt(p, 0, 0, kRed), y = pixelAt(p, 0, 0, kBlue);

    REQUIRE(p.addLayer(x, master, id, 0));   // its bottom: under paint, still in it
    CHECK(layerIds(p, master) == std::vector{ s.background, x, s.paint });
    CHECK(stackOf(p, master).groups == std::vector<StackGroup>{ { kFirst, 1, 3 } });
    REQUIRE(p.addLayer(y, master, id, 2));   // its top
    CHECK(layerIds(p, master) == std::vector{ s.background, x, s.paint, y });
    CHECK(stackOf(p, master).groups == std::vector<StackGroup>{ { kFirst, 1, 4 } });
    REQUIRE(p.addLayer(y, master, kNoArtboard, 0));              // one already in it moves: to the bottom of the stack
    CHECK(layerIds(p, master) == std::vector{ y, s.background, x, s.paint });
    CHECK(stackOf(p, master).groups == std::vector<StackGroup>{ { kFirst, 2, 4 } });
    CHECK_FALSE(p.addLayer(master, master, kNoArtboard, 0));     // never itself
}

TEST_CASE("rows removed from a stack: layers unlinked, artboards gone, one undo step")
{
    StarterWorkspace s;
    Workspace& p = s.workspace;
    const NodeId master = s.master;
    const ArtboardId id = p.addArtboard({ 0, 0, 9, 9 });
    REQUIRE(p.moveStackRows(master, { { s.paint }, {}, id, 0 }));
    step(p, "Remove", [&] { REQUIRE(p.removeStackRows(master, { s.background }, { id })); });
    CHECK(layerIds(p, master) == std::vector{ s.paint });   // the artboard's layer stays
    CHECK(p.artboards().empty());
    CHECK(p.find(s.background));                             // the node itself stays in the graph
    CHECK_FALSE(p.removeStackRows(master, { s.background }, {}));   // not in it any more
    REQUIRE(p.undo());
    CHECK(layerIds(p, master) == std::vector{ s.background, s.paint });
    REQUIRE(p.artboard(id));
    CHECK(stackOf(p, master).groups == std::vector<StackGroup>{ { kFirst, 1, 2 } });
}

TEST_CASE("a lifted link lands at a place in a stack")
{
    Document document;
    Workspace& p = document.workspace();
    NodeId a = kNoNodeId, b = kNoNodeId;
    p.init([&](Workspace& seeded) {
        TileGrid one, two;
        fillPixel(one, 0, 0, kRed);
        fillPixel(two, 0, 0, kBlue);
        a = seeded.importTiles(one, "", {});
        b = seeded.importTiles(two, "", {});   // [a b]
    });
    ActionRegistry& actions = document.actions();
    const NodeId master = p.master().id();
    using namespace DocArgs;
    const ArtboardId board{ uint32_t(actions.run(Actions::ArtboardAdd, { { kMinX, 0 }, { kMinY, 0 }, { kMaxX, 9 }, { kMaxY, 9 } }).made) };
    // b lifted off its row and dropped into the artboard, on its own stack.
    REQUIRE(actions.beginGesture(Actions::LinkMove, { { kSource, b }, { kFrom, master }, { kSlot, InputSlot::stackLayer(1) } }));
    REQUIRE(actions.endGesture(Actions::LinkMove, { { kTo, master }, { kToSlot, InputSlot::newLayer(0) }, { kIntoArtboard, board },
                                                    { kPlaceAt, size_t(0) } }));
    CHECK(layerIds(p, master) == std::vector{ a, b });
    CHECK(stackOf(p, master).groups == std::vector<StackGroup>{ { board, 1, 2 } });
    REQUIRE(actions.run(Actions::EditUndo));
    CHECK(stackOf(p, master).groups == std::vector<StackGroup>{ { board, 2, 2 } });
}
