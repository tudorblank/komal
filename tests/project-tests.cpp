#include "document/graph-state.hpp"
#include "node/node-kinds.hpp"
#include "helpers.hpp"

#include <vector>

TEST_CASE("links: no cycles, the master feeds nothing, a slot takes one source")
{
    StarterProject s;
    Project& p = s.project;
    const NodeId m1 = p.createNode(NodeType::Move, 0, 0);
    const NodeId m2 = p.createNode(NodeType::Move, 0, 0);
    CHECK(p.connect(m1, m2, InputSlot::input()));
    CHECK_FALSE(p.canConnect(m2, m1, InputSlot::input()));          // a cycle
    CHECK_FALSE(p.canConnect(m1, m1, InputSlot::input()));          // itself
    CHECK_FALSE(p.canConnect(s.master, m1, InputSlot::input()));    // the master's output is the canvas's
    CHECK_FALSE(p.canConnect(m1, s.background, InputSlot::input()));   // a raster takes no input
    CHECK(p.connect(s.background, m1, InputSlot::input()));         // replaces nothing: m1 had none
    CHECK(p.inputAt(m1, InputSlot::input())->id() == s.background);
    CHECK(p.disconnect(m1, InputSlot::input()));
    CHECK_FALSE(p.disconnect(m1, InputSlot::input()));              // nothing left there
}

TEST_CASE("deleting a node heals the links through it")
{
    StarterProject s;
    Project& p = s.project;
    const NodeId move = p.createNode(NodeType::Move, 0, 0);
    REQUIRE(p.connect(s.background, move, InputSlot::input()));
    REQUIRE(p.connect(move, s.master, InputSlot::stackLayer(0)));   // the move takes the background's place
    CHECK(p.master().layers()[0]->id() == move);
    CHECK(p.deleteNodes({ move }));
    CHECK_FALSE(p.find(move));
    REQUIRE(p.master().layers().size() == 2);
    CHECK(p.master().layers()[0]->id() == s.background);            // fed again from upstream
    CHECK_FALSE(p.deleteNodes({ s.master }));                      // never the master
}

TEST_CASE("undo and redo restore the graph and its settings")
{
    StarterProject s;
    Project& p = s.project;
    NodeId move = kNoNodeId;
    step(p, "Add", [&] {
        move = p.createNode(NodeType::Move, 10, 20);
        p.connect(s.background, move, InputSlot::input());
    });
    step(p, "Offset", [&] { p.setNodeSettings(move, MoveSettings{ 30, -5 }); });
    step(p, "Scale", [&] { p.setSignalScale(s.paint, NodeSignal::Main, NodeChannel::A, 0.25f); });
    REQUIRE(p.history().size() == 4);

    CHECK(p.undo());
    CHECK(p.find(s.paint)->signal(NodeSignal::Main).scale[0] == 1.0f);
    CHECK(p.undo());
    CHECK(static_cast<const MoveNode*>(p.find(move))->offsetX() == 0);
    CHECK(p.undo());
    CHECK_FALSE(p.find(move));
    CHECK_FALSE(p.undo());   // at the start
    CHECK(p.redo());
    REQUIRE(p.find(move));
    CHECK(p.inputAt(move, InputSlot::input())->id() == s.background);   // back as itself, linked
    CHECK(p.goToHistory(StepId(3)));
    CHECK(static_cast<const MoveNode*>(p.find(move))->offsetX() == 30);
    CHECK(p.find(s.paint)->signal(NodeSignal::Main).scale[0] == 0.25f);
}

TEST_CASE("a step that changes nothing isn't kept")
{
    StarterProject s;
    Project& p = s.project;
    step(p, "Nothing", [&] { p.setSignalScale(s.paint, NodeSignal::Main, NodeChannel::A, 1.0f); });
    CHECK(p.history().size() == 1);
}

TEST_CASE("a graph snapshot restores what changed since")
{
    StarterProject s;
    Project& p = s.project;
    const NodeId move = p.createNode(NodeType::Move, 0, 0);
    const GraphState before = [&] {
        p.beginStep("capture");   // snapshots are taken around steps: read the current one
        p.endStep();
        return *p.history().step(p.history().current()).graph;
    }();
    step(p, "Change", [&] {
        p.setNodePosition(move, 50, 60);
        p.setNodeSettings(move, MoveSettings{ 4, 4 });
        p.setNodeSettings(s.background, RasterSettings{ true, false });
        p.connect(s.paint, move, InputSlot::input());
    });
    CHECK(p.undo());
    const Node& m = *p.find(move);
    CHECK(m.x() == 0.0f);
    CHECK(static_cast<const MoveNode&>(m).offsetX() == 0);
    CHECK_FALSE(static_cast<const RasterRootNode*>(p.find(s.background))->locked());
    CHECK_FALSE(p.inputAt(move, InputSlot::input()));
    (void)before;
}

TEST_CASE("a dragged value is previewed, then applied once or cancelled")
{
    StarterProject s;
    Project& p = s.project;
    int changes = 0;
    const Connection c = p.changed.connect([&](const ProjectEvent& e) { if (e.type == ProjectEventType::NodeChanged) ++changes; });
    p.previewEdit(ScaleEdit{ s.paint, NodeSignal::Main, NodeChannel::A, 0.5f });
    REQUIRE(p.preview());
    CHECK(p.find(s.paint)->signal(NodeSignal::Main).scale[0] == 1.0f);   // the graph waits
    p.previewEdit(ScaleEdit{ s.paint, NodeSignal::Main, NodeChannel::A, 0.3f });
    CHECK(changes == 0);
    p.applyPreview();
    CHECK_FALSE(p.preview());
    CHECK(p.find(s.paint)->signal(NodeSignal::Main).scale[0] == 0.3f);
    CHECK(changes == 1);

    p.previewEdit(ScaleEdit{ s.paint, NodeSignal::Main, NodeChannel::A, 0.9f });
    p.cancelPreview();
    CHECK(p.find(s.paint)->signal(NodeSignal::Main).scale[0] == 0.3f);
}

TEST_CASE("a stroke changes the raster once, when it ends: one undo step")
{
    StarterProject s;
    Project& p = s.project;
    REQUIRE(p.activeRasterId() == s.paint);
    BrushParams brush;
    brush.diameter = 6.0f;
    p.beginStroke(brush, { 100.0f, 100.0f, 1.0f });
    const StrokePoint path[] = { { 120.0f, 100.0f, 1.0f }, { 140.0f, 100.0f, 1.0f } };
    p.strokeTo(path);
    REQUIRE(p.strokeActive());
    REQUIRE(p.preview());
    CHECK(p.preview()->stroke());
    CHECK(pixelOf(p, s.paint, 120, 100).a == 0);   // only the coverage grew
    CHECK_FALSE(p.stroke().coverage().getReadTile(1, 1) == nullptr);
    p.endStroke();
    CHECK_FALSE(p.strokeActive());
    CHECK_FALSE(p.preview());
    CHECK(pixelOf(p, s.paint, 120, 100).a == 255);
    CHECK(p.history().size() == 2);
    CHECK(p.undo());
    CHECK(pixelOf(p, s.paint, 120, 100).a == 0);
    CHECK(p.redo());
    CHECK(pixelOf(p, s.paint, 120, 100).a == 255);
}

TEST_CASE("a stroke's bounds while it's painted are the raster's once it ends")
{
    // Soft and faint: the rim's least coverage rounds away. A short stroke that doubles back.
    StarterProject s;
    Project& p = s.project;
    BrushParams brush;
    brush.diameter = 9.0f;
    brush.hardness = 0.0f;
    brush.opacity  = 40;
    brush.color    = { 255, 255, 255, 128 };
    const BoundsI before = p.find(s.paint)->bounds();
    p.beginStroke(brush, { 300.5f, 300.5f, 1.0f });
    const StrokePoint path[] = { { 311.0f, 304.0f, 1.0f }, { 303.0f, 301.0f, 1.0f } };
    p.strokeTo(path);
    const BoundsI stroke = p.stroke().bounds();
    CHECK(stroke.width() < Tile::kSize);   // the pixels, not whole tiles
    BoundsI expected = before;
    expected.expand(stroke);
    p.endStroke();
    CHECK(p.find(s.paint)->bounds() == expected);
}

TEST_CASE("alpha lock: a stroke keeps the raster's transparency; a locked raster takes none")
{
    StarterProject s;
    Project& p = s.project;
    p.setNodeSettings(s.paint, RasterSettings{ false, true });
    BrushParams brush;
    brush.diameter = 200.0f;
    brush.color = { 0, 0, 255, 255 };
    p.beginStroke(brush, { -48.0f, -48.0f, 1.0f });   // over the disc and around it
    p.endStroke();
    const RGBA inside = pixelOf(p, s.paint, -48, -48);
    CHECK(inside.a == 255);
    CHECK(inside.b == 255);
    CHECK(pixelOf(p, s.paint, 40, 40).a == 0);        // outside the disc: still nothing

    p.setNodeSettings(s.paint, RasterSettings{ true, true });
    CHECK_FALSE(p.activeRasterEditable());
    p.beginStroke(brush, { 0.0f, 0.0f, 1.0f });
    CHECK_FALSE(p.strokeActive());
}

TEST_CASE("the selection follows the nodes: a deleted node leaves it")
{
    StarterProject s;
    Project& p = s.project;
    CHECK(p.setSelection({ { s.background, s.paint }, s.background }));
    CHECK_FALSE(p.setSelection({ { s.background, s.paint }, s.background }));   // the same: no change
    p.deleteNodes({ s.background });
    CHECK(p.selection().size() == 1);
    CHECK(p.selection().contains(s.paint));
    CHECK(p.selection().anchor() == kNoNodeId);
    CHECK(p.setSelection({ { NodeId(999) }, NodeId(999) }));   // unknown ids are left out: nothing selected
    CHECK(p.selection().empty());
    CHECK(p.selection().anchor() == kNoNodeId);
}

TEST_CASE("selection rules: click, toggle, chain along links")
{
    StarterProject s;
    Project& p = s.project;
    const NodeSelection one = NodeSelection::only(s.background);
    CHECK(one.single() == s.background);
    const NodeSelection two = one.toggled(s.paint);
    CHECK(two.size() == 2);
    CHECK(two.toggled(s.paint).size() == 1);
    const NodeSelection chain = one.chained(p, s.paint, false);   // background - master - paint
    CHECK(chain.size() == 3);
    CHECK(chain.contains(s.master));
    CHECK(chain.anchor() == s.background);
}

TEST_CASE("a dragged offset is the edit's alone: the Move keeps its own until applied")
{
    StarterProject s;
    Project& p = s.project;
    const NodeId move = p.createNode(NodeType::Move, 0, 0);
    REQUIRE(p.connect(s.background, move, InputSlot::input()));
    const BoundsI before = p.find(move)->bounds();
    CHECK(p.previewEdit(OffsetEdit{ move, 100, 0 }));
    CHECK(static_cast<const MoveNode*>(p.find(move))->offsetX() == 0);
    CHECK(p.find(move)->bounds() == before);                  // the document waits, bounds too
    CHECK(p.preview()->offsetOf(move)->x == 100);
    CHECK_FALSE(p.preview()->offsetOf(s.background));
    p.applyPreview();
    CHECK(static_cast<const MoveNode*>(p.find(move))->offsetX() == 100);
    CHECK(p.find(move)->bounds() == before.offsetBy(100, 0));
}

TEST_CASE("an edit that doesn't fit the document is refused")
{
    StarterProject s;
    Project& p = s.project;
    CHECK_FALSE(p.previewEdit(ScaleEdit{ s.paint, NodeSignal::Main, NodeChannel::A, 1.5f }));    // out of range
    CHECK_FALSE(p.previewEdit(ScaleEdit{ s.paint, NodeSignal::Wet, NodeChannel::A, 0.5f }));     // a raster has no wet
    CHECK_FALSE(p.previewEdit(OffsetEdit{ s.paint, 1, 1 }));                                     // not a Move
    CHECK_FALSE(p.previewEdit(OffsetEdit{ p.createNode(NodeType::Move, 0, 0), 1 << 30, 0 }));    // out of the world
    CHECK_FALSE(p.previewEdit(NodesMoveEdit{ { NodeId(999) }, 1.0f, 1.0f }));                    // not there
    CHECK_FALSE(p.previewEdit(LinkMoveEdit{ s.paint, s.master, InputSlot::stackLayer(0) }));     // that slot is fed by another
    CHECK_FALSE(p.previewEdit(StrokeEdit{ s.paint, 1 }));                                        // strokes begin with beginStroke()
    CHECK_FALSE(p.preview());
}

TEST_CASE("nodes dragged in the graph move once, when let go")
{
    StarterProject s;
    Project& p = s.project;
    const float x = p.find(s.paint)->x();
    CHECK(p.previewEdit(NodesMoveEdit{ { s.paint, s.background }, 25.0f, -10.0f }));
    CHECK(p.find(s.paint)->x() == x);
    step(p, "Move Nodes", [&] { p.applyPreview(); });
    CHECK(p.find(s.paint)->x() == x + 25.0f);
    CHECK(p.history().size() == 2);
    p.previewEdit(NodesMoveEdit{ { s.paint }, 5.0f, 5.0f });
    p.cancelPreview();
    CHECK(p.find(s.paint)->x() == x + 25.0f);
}

TEST_CASE("a lifted link changes nothing until it lands; deleting what it names drops it")
{
    StarterProject s;
    Project& p = s.project;
    CHECK(p.previewEdit(LinkMoveEdit{ s.paint, s.master, InputSlot::stackLayer(1) }));
    CHECK(p.master().layers().size() == 2);   // still linked while carried
    p.deleteNodes({ s.paint });
    CHECK_FALSE(p.preview());
}

TEST_CASE("a node's settings are its kind's own: anything else is refused")
{
    StarterProject s;
    Project& p = s.project;
    const NodeId move = p.createNode(NodeType::Move, 0, 0);
    CHECK(p.setNodeSettings(move, MoveSettings{ 3, 4 }));
    CHECK_FALSE(p.setNodeSettings(move, MoveSettings{ 3, 4 }));            // no change
    CHECK_FALSE(p.setNodeSettings(move, RasterSettings{ true, false }));   // not a Move's
    CHECK_FALSE(p.setNodeSettings(move, MoveSettings{ 1 << 30, 0 }));      // out of the world
    CHECK_FALSE(p.setNodeSettings(s.master, MoveSettings{ 1, 1 }));        // a compositor has none
    CHECK(std::get<MoveSettings>(p.find(move)->settings()) == MoveSettings{ 3, 4 });
    CHECK(p.setNodeSettings(s.paint, RasterSettings{ true, false }));
    CHECK(static_cast<const RasterRootNode*>(p.find(s.paint))->locked());
}

TEST_CASE("a kind's input shape comes from its class")
{
    CHECK(nodeKind(NodeType::Raster).inputs == InputShape::None);
    CHECK(nodeKind(NodeType::Reference).inputs == InputShape::One);
    CHECK(nodeKind(NodeType::Move).inputs == InputShape::One);
    CHECK(nodeKind(NodeType::Compositor).inputs == InputShape::Stack);
}

TEST_CASE("links into a node: inputs, layers and masks, each with its slot")
{
    StarterProject s;
    Project& p = s.project;
    const NodeId move = p.createNode(NodeType::Move, 0, 0);
    REQUIRE(p.connect(s.background, move, InputSlot::input()));
    REQUIRE(p.connect(s.paint, move, InputSlot::mask(NodeSignal::Main, NodeChannel::A)));
    const auto into = Links::into(*p.find(move));
    REQUIRE(into.size() == 2);
    CHECK(into[0].first->id() == s.background);
    CHECK(into[0].second == InputSlot::input());
    CHECK(into[1].second == InputSlot::mask(NodeSignal::Main, NodeChannel::A));
    const auto layers = Links::into(p.master());
    REQUIRE(layers.size() == 2);
    CHECK(layers[1].second == InputSlot::stackLayer(1));
    InputSlot slot;
    CHECK(p.preferredInput(s.master, slot));
    CHECK(slot == InputSlot::newLayer(2));
    CHECK_FALSE(p.preferredInput(s.paint, slot));   // a raster takes no input
}

TEST_CASE("deleting a chain heals through every deleted node with one input")
{
    StarterProject s;
    Project& p = s.project;
    const NodeId a = p.createNode(NodeType::Move, 0, 0);
    const NodeId b = p.createNode(NodeType::Reference, 0, 0);
    REQUIRE(p.connect(s.background, a, InputSlot::input()));
    REQUIRE(p.connect(a, b, InputSlot::input()));
    REQUIRE(p.connect(b, s.master, InputSlot::stackLayer(0)));
    CHECK(p.deleteNodes({ a, b }));
    CHECK(p.master().layers()[0]->id() == s.background);
}
