#include "command/actions.hpp"
#include "document/document-actions.hpp"
#include "document/workspace.hpp"
#include "helpers.hpp"

#include <doctest/doctest.h>
#include <limits>

namespace {

constexpr ArgKey<NodeId>              kNode{ "node" };
constexpr ArgKey<std::vector<NodeId>> kNodes{ "nodes" };
constexpr ArgKey<float>               kValue{ "value" };
constexpr ArgKey<std::string>         kMode{ "mode" };
constexpr ArgKey<bool>                kOn{ "on" };
constexpr ArgKey<float>               kDx{ "dx" };
constexpr ArgKey<int>                 kN{ "n" };
// The same names as other types: what a caller could write by mistake.
constexpr ArgKey<int>                 kOnAsInt{ "on" };
constexpr ArgKey<std::vector<int>>    kNodesAsInts{ "nodes" };
constexpr ArgKey<int>                 kExtra{ "extra" };
constexpr ArgKey<double>              kDxWide{ "dx" };

}

TEST_CASE("arguments are checked against their spec")
{
    const ArgSpec specs[] = { Arg::id(kNode), Arg::number(kValue, 0.0, 1.0), Arg::choice(kMode, { "paint", "erase" }),
                              Arg::flag(kOn).optional(), Arg::ids(kNodes).optional() };
    const NodeId n3 = NodeId(3);
    CHECK(checkArgs(specs, { { kNode, n3 }, { kValue, 0.5f }, { kMode, "paint" } }).empty());
    CHECK(checkArgs(specs, { { kNode, n3 }, { kValue, 1.0f }, { kMode, "erase" }, { kOn, true } }).empty());
    CHECK(checkArgs(specs, { { kNode, n3 }, { kN, 1 } }) == "unknown argument 'n'");
    CHECK_FALSE(checkArgs(specs, { { kValue, 0.5f }, { kMode, "paint" } }).empty());                          // missing
    CHECK_FALSE(checkArgs(specs, { { kNode, NodeId(0) }, { kValue, 0.5f }, { kMode, "paint" } }).empty());   // not an id
    CHECK_FALSE(checkArgs(specs, { { kNode, n3 }, { kValue, 1.5f }, { kMode, "paint" } }).empty());           // out of range
    const ArgSpec any[] = { Arg::number(kDx) };                                                                // no range: still finite
    CHECK_FALSE(checkArgs(any, { { kDxWide, std::numeric_limits<double>::infinity() } }).empty());
    CHECK_FALSE(checkArgs(any, { { kDxWide, std::numeric_limits<double>::quiet_NaN() } }).empty());
    CHECK_FALSE(checkArgs(specs, { { kNode, n3 }, { kValue, 0.5f }, { kMode, "blur" } }).empty());            // not a choice
    CHECK_FALSE(checkArgs(specs, { { kNode, n3 }, { kValue, 0.5f }, { kMode, "paint" }, { kExtra, 1 } }).empty());   // unknown
    CHECK_FALSE(checkArgs(specs, { { kNode, n3 }, { kValue, 0.5f }, { kMode, "paint" }, { kOnAsInt, 1 } }).empty()); // not a flag
    CHECK_FALSE(checkArgs(specs, { { kNode, n3 }, { kValue, 0.5f }, { kMode, "paint" }, { kNodesAsInts, { 2, 0 } } }).empty());
}

TEST_CASE("arguments read back as the types they were written as")
{
    constexpr ArgKey<NodeSignal> kSignal{ "signal" };
    constexpr ArgKey<InputSlot>  kSlot{ "slot" };
    constexpr ArgKey<Color8>     kColor{ "color" };
    constexpr ArgKey<size_t>     kIndex{ "index" };
    const std::vector<NodeId> ids{ NodeId(4), NodeId(9) };
    const ActionArgs a{ { kNodes, ids }, { kSignal, NodeSignal::Wet }, { kSlot, InputSlot::mask(NodeSignal::Dry, NodeChannel::G) },
                        { kColor, Color8{ 255, 128, 0, 200 } }, { kIndex, size_t(3) }, { kValue, 0.25f }, { kOn, true } };
    CHECK(a.get(kNodes) == ids);
    CHECK(a.get(kSignal) == NodeSignal::Wet);
    CHECK(a.get(kSlot) == InputSlot::mask(NodeSignal::Dry, NodeChannel::G));
    CHECK(a.get(kColor) == Color8{ 255, 128, 0, 200 });
    CHECK(a.get(kIndex) == 3);
    CHECK(a.get(kValue) == 0.25f);
    CHECK(a.get(kOn));
    CHECK_FALSE(a.has(kNode));
    CHECK(a.get(kNode, NodeId(5)) == NodeId(5));   // left out: what's given instead
    CHECK(a.get(kValue, 1.0f) == 0.25f);
    CHECK(a.format() == "nodes=[4,9] signal=2 slot=mask:dry.g color=#FF8000C8 index=3 value=0.25 on=on");
}

TEST_CASE("colours as text")
{
    CHECK(Color8{ 1, 171, 255, 0 }.text() == "#01ABFF00");
    CHECK(Color8::parse("#01abff00") == Color8{ 1, 171, 255, 0 });
    for (const char* bad : { "", "#", "01ABFF00", "#01ABFF0", "#01ABFF000", "#01ABFG00", "#01 BFF00" }) {
        CAPTURE(bad);
        CHECK_FALSE(Color8::parse(bad));
    }
}

TEST_CASE("the registry refuses malformed actions and bad arguments")
{
    ActionRegistry r;
    int runs = 0;
    r.add({ .id = ActionId("test.run"), .title = "Run", .kind = ActionKind::Interface, .args = { Arg::integer(kN, 0, 10) },
            .run = [&](const ActionArgs& a) { runs += a.get(kN); } });
    CHECK(r.find(ActionId("test.run")));
    CHECK(r.run(ActionId("test.run"), { { kN, 2 } }));
    const LogCapture log;
    CHECK_FALSE(r.run(ActionId("test.run"), { { kN, 20 } }));   // refused: out of range
    CHECK(log.saw("'n' is out of range"));
    CHECK(runs == 2);
}

TEST_CASE("a gesture's end sees begin's arguments, and a cancel ends it")
{
    ActionRegistry r;
    int began = 0, ended = 0, cancelled = 0;
    NodeId seen = kNoNodeId;
    r.add({ .id = ActionId("test.drag"), .title = "Drag", .kind = ActionKind::Gesture, .args = { Arg::id(kNode) },
            .endArgs = { Arg::number(kDx) },
            .begin  = [&] { ++began; },
            .end    = [&](const ActionArgs& a) { ++ended; seen = a.get(kNode); },
            .cancel = [&] { ++cancelled; } });
    REQUIRE(r.beginGesture(ActionId("test.drag"), { { kNode, NodeId(7) } }));
    CHECK(r.inGesture(ActionId("test.drag")));
    CHECK(r.endGesture(ActionId("test.drag"), { { kDx, 1.0f } }));
    CHECK(seen == NodeId(7));
    REQUIRE(r.beginGesture(ActionId("test.drag"), { { kNode, NodeId(7) } }));
    r.cancelGesture(ActionId("test.drag"));
    CHECK_FALSE(r.inGesture(ActionId("test.drag")));
    CHECK(began == 2);
    CHECK(ended == 1);
    CHECK(cancelled == 1);
}

TEST_CASE("a gesture's updates are checked: a mistaken one is refused, the gesture goes on")
{
    ActionRegistry r;
    std::vector<float> seen;
    r.add({ .id = ActionId("test.scrub"), .title = "Scrub", .kind = ActionKind::Gesture, .args = { Arg::id(kNode) },
            .updateArgs = { Arg::number(kValue, 0.0, 1.0) },
            .begin  = [] { return true; },
            .update = [&](const ActionArgs& a) { seen.push_back(a.get(kValue)); },
            .end    = [] {} });
    REQUIRE(r.beginGesture(ActionId("test.scrub"), { { kNode, NodeId(1) } }));
    r.updateGesture(ActionId("test.scrub"), { { kValue, 0.25f } });
    {
        const LogCapture log;
        r.updateGesture(ActionId("test.scrub"), { { kValue, 4.0f } });   // out of range
        r.updateGesture(ActionId("test.scrub"), { { kExtra, 1 } });      // not its argument
        CHECK(log.saw("refused"));
    }
    CHECK(r.inGesture(ActionId("test.scrub")));
    r.updateGesture(ActionId("test.scrub"), { { kValue, 0.5f } });
    CHECK(seen == std::vector<float>{ 0.25f, 0.5f });
    CHECK(r.endGesture(ActionId("test.scrub")));
}

TEST_CASE("a parsed argument must read back as its type")
{
    constexpr ArgKey<InputSlot>   kSlot{ "slot" };
    constexpr ArgKey<std::string> kSlotText{ "slot" };   // as a log or a script could write it
    const ArgSpec specs[] = { Arg::parsed(kSlot) };
    CHECK(checkArgs(specs, { { kSlot, InputSlot::newLayer(2) } }).empty());
    CHECK(checkArgs(specs, { { kSlotText, "layer:9" } }).empty());
    CHECK_FALSE(checkArgs(specs, { { kSlotText, "layer:9x" } }).empty());
}

TEST_CASE("a cancelled stroke leaves the raster as it was, and no step")
{
    StarterProject s;
    Project& p = s.project;
    ActionRegistry actions;
    registerDocumentActions(actions, p);
    REQUIRE(actions.beginGesture(Actions::BrushStroke, strokeArgs(p.activeRasterId(), BrushParams{}, { 500.0f, 500.0f, 1.0f }, false)));
    CHECK(p.strokeActive());
    actions.cancelGesture(Actions::BrushStroke);
    CHECK_FALSE(p.strokeActive());
    CHECK_FALSE(p.preview());
    CHECK(pixelOf(p, s.paint, 500, 500).a == 0);
    CHECK(p.history().size() == 1);
    CHECK(actions.log().back().status == ActionLogEntry::Status::Cancelled);
}

TEST_CASE("a stroke is made from its arguments alone: what the log keeps")
{
    StarterProject s;
    Project& p = s.project;
    ActionRegistry actions;
    registerDocumentActions(actions, p);
    BrushParams brush;
    brush.diameter = 9.0f;
    brush.hardness = 1.0f;
    brush.color    = { 255, 0, 0, 255 };
    const ActionArgs args = strokeArgs(p.activeRasterId(), brush, { 300.0f, 200.0f, 1.0f }, true);

    // Another raster than the one being painted: refused.
    ActionArgs elsewhere = args;
    elsewhere.add({ DocArgs::kRaster, s.master });
    {
        const LogCapture log;
        CHECK_FALSE(actions.beginGesture(Actions::BrushStroke, elsewhere));
    }
    // Out of the brush's range: refused.
    ActionArgs huge = args;
    huge.add({ DocArgs::kSize, BrushParams::kMaxDiameter * 2.0f });
    {
        const LogCapture log;
        CHECK_FALSE(actions.beginGesture(Actions::BrushStroke, huge));
    }

    REQUIRE(actions.beginGesture(Actions::BrushStroke, args));
    CHECK(actions.endGesture(Actions::BrushStroke, { { DocArgs::kPoints, 1 } }));
    CHECK(pixelOf(p, s.paint, 300, 200) == RGBA{ 255, 0, 0, 255 });   // the first dab, in its colour
    CHECK(pixelOf(p, s.paint, 300, 206).a == 0);                       // its size
    CHECK(actions.log().back().args.find("color=#FF0000FF") != std::string::npos);
    CHECK(actions.log().back().args.find("device=pen") != std::string::npos);
}

TEST_CASE("a workspace is driven by its actions alone, no interface")
{
    Workspace workspace;
    workspace.project().init([](Project& seeded) {   // one raster to start from
        TileGrid tiles;
        fillPixel(tiles, 0, 0, { 255, 0, 0, 255 });
        seeded.importTiles(tiles, "", {});
    });
    const Project& p = workspace.project();
    const ActionResult made = workspace.actions().run(Actions::NodeAdd, { { DocArgs::kType, "move" }, { DocArgs::kX, 10.0f }, { DocArgs::kY, 20.0f },
                                                                        { DocArgs::kFrom, p.activeRasterId() } });
    REQUIRE(made);
    const NodeId id = NodeId(made.made);
    REQUIRE(p.find(id));
    CHECK(p.find(id)->x() == 10.0f);
    CHECK(workspace.actions().run(Actions::MoveSet, { { DocArgs::kNode, id }, { DocArgs::kOffsetX, 5 }, { DocArgs::kOffsetY, -3 } }));
    CHECK(workspace.actions().run(Actions::EditUndo));
    CHECK(workspace.actions().run(Actions::EditUndo));
    CHECK_FALSE(p.find(id));   // both steps undone
    CHECK(workspace.actions().log().size() >= 4);
}
