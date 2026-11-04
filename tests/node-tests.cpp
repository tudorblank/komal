// The node graph's render path and change propagation, driven through the Workspace.

#include "helpers.hpp"

#include <chrono>

TEST_CASE("signals: a channel's scale shapes the output")
{
    StarterWorkspace s;
    Workspace& p = s.workspace;
    const NodeId r = rasterWith(p, [](TileGrid& g) { fillPixel(g, 3, 3, RGBA{ 200, 100, 50, 255 }); });
    CHECK(rendered(p, r, 3, 3) == RGBA{ 200, 100, 50, 255 });
    REQUIRE(p.setSignalScale(r, NodeSignal::Main, NodeChannel::A, 0.5f));
    CHECK(rendered(p, r, 3, 3) == RGBA{ 100, 50, 25, 128 });   // the opacity: everything at half
    REQUIRE(p.setSignalScale(r, NodeSignal::Main, NodeChannel::A, 1.0f));
    REQUIRE(p.setSignalScale(r, NodeSignal::Main, NodeChannel::R, 0.0f));
    CHECK(rendered(p, r, 3, 3) == RGBA{ 0, 100, 50, 255 });    // red only
    CHECK_FALSE(p.setSignalScale(r, NodeSignal::Main, NodeChannel::G, 1.5f));   // out of range: refused
}

TEST_CASE("masks: black hides, white keeps, transparent leaves it alone")
{
    StarterWorkspace s;
    Workspace& p = s.workspace;
    const NodeId r = rasterWith(p, [](TileGrid& g) {
        for (int x = 0; x < 3; ++x) fillPixel(g, x, 0, RGBA{ 255, 0, 0, 255 });
    });
    const NodeId mask = rasterWith(p, [](TileGrid& g) {
        fillPixel(g, 0, 0, RGBA{ 0, 0, 0, 255 });         // black
        fillPixel(g, 1, 0, RGBA{ 255, 255, 255, 255 });   // white
    });                                                    // (2, 0): nothing
    REQUIRE(p.connect(mask, r, InputSlot::mask(NodeSignal::Main, NodeChannel::A)));
    CHECK(rendered(p, r, 0, 0).a == 0);
    CHECK(rendered(p, r, 1, 0) == RGBA{ 255, 0, 0, 255 });
    CHECK(rendered(p, r, 2, 0) == RGBA{ 255, 0, 0, 255 });
}

TEST_CASE("a Move shifts by whole tiles, by parts of one, and either way")
{
    StarterWorkspace s;
    Workspace& p = s.workspace;
    const NodeId r = rasterWith(p, [](TileGrid& g) { fillPixel(g, 5, 5, RGBA{ 1, 2, 3, 255 }); });
    const NodeId move = p.addNode(NodeType::Move, 0, 0, {});
    REQUIRE(p.connect(r, move, InputSlot::input()));
    for (const auto [dx, dy] : { std::pair{ 64, -128 }, std::pair{ -70, 3 }, std::pair{ -1, -1 }, std::pair{ 0, 0 } }) {
        if (dx || dy) REQUIRE(p.setNodeSettings(move, MoveSettings{ dx, dy }));
        else REQUIRE(p.setNodeSettings(move, MoveSettings{ 0, 0 }));
        CHECK(rendered(p, move, 5 + dx, 5 + dy) == RGBA{ 1, 2, 3, 255 });
        if (dx || dy) CHECK(rendered(p, move, 5, 5) == transparent());
        CHECK(p.find(move)->bounds() == BoundsI{ 5 + dx, 5 + dy, 5 + dx, 5 + dy });
    }
}

TEST_CASE("dry and wet: the input where it was, under the moved one")
{
    StarterWorkspace s;
    Workspace& p = s.workspace;
    const NodeId r = rasterWith(p, [](TileGrid& g) { fillPixel(g, 0, 0, RGBA{ 0, 0, 255, 255 }); });
    const NodeId move = p.addNode(NodeType::Move, 0, 0, {});
    REQUIRE(p.connect(r, move, InputSlot::input()));
    REQUIRE(p.setNodeSettings(move, MoveSettings{ 10, 0 }));
    REQUIRE(p.setSignalScale(move, NodeSignal::Dry, NodeChannel::A, 1.0f));   // dry on: both
    CHECK(rendered(p, move, 0, 0) == RGBA{ 0, 0, 255, 255 });
    CHECK(rendered(p, move, 10, 0) == RGBA{ 0, 0, 255, 255 });
    CHECK(p.find(move)->bounds() == BoundsI{ 0, 0, 10, 0 });
    REQUIRE(p.setSignalScale(move, NodeSignal::Wet, NodeChannel::A, 0.0f));   // wet off: the input, unmoved
    CHECK(rendered(p, move, 10, 0) == transparent());
    CHECK(p.find(move)->bounds() == BoundsI{ 0, 0, 0, 0 });
}

TEST_CASE("the master's cached composite follows every change under it")
{
    StarterWorkspace s;
    Workspace& p = s.workspace;
    const NodeId bottom = rasterWith(p, [](TileGrid& g) { fillPixel(g, 900, 900, RGBA{ 255, 0, 0, 255 }); });
    const NodeId top = rasterWith(p, [](TileGrid& g) { fillPixel(g, 900, 900, RGBA{ 0, 0, 128, 128 }); });
    REQUIRE(p.connect(bottom, s.master, InputSlot::newLayer(2)));
    CHECK(rendered(p, s.master, 900, 900) == RGBA{ 255, 0, 0, 255 });   // cached now
    CHECK(rendered(p, s.master, 2000, 2000) == transparent());          // cached as empty
    REQUIRE(p.connect(top, s.master, InputSlot::newLayer(3)));
    CHECK(rendered(p, s.master, 900, 900) == RGBA{ 127, 0, 128, 255 });   // half blue over red
    REQUIRE(p.setSignalScale(top, NodeSignal::Main, NodeChannel::A, 0.0f));
    CHECK(rendered(p, s.master, 900, 900) == RGBA{ 255, 0, 0, 255 });
    // Painting where it was cached as empty shows up.
    REQUIRE(p.setActiveRaster(bottom));
    BrushParams brush;
    brush.diameter = 4.0f;
    p.beginStroke(brush, { 2000.5f, 2000.5f, 1.0f });
    p.endStroke();
    CHECK(rendered(p, s.master, 2000, 2000).a == 255);
    REQUIRE(p.moveStackRows(s.master, { { p.master().layers()[3]->id() }, {}, kNoArtboard, 0 }));   // the top to the bottom: red covers it again
    REQUIRE(p.setSignalScale(top, NodeSignal::Main, NodeChannel::A, 1.0f));
    CHECK(rendered(p, s.master, 900, 900) == RGBA{ 255, 0, 0, 255 });
}

TEST_CASE("a reader of a node's output hears which tiles changed")
{
    StarterWorkspace s;
    Workspace& p = s.workspace;
    const std::shared_ptr<TileWatch> watch = p.master().watchOutput();
    TileSet changed;
    CHECK(watch->take(changed));   // a new reader reads everything once
    CHECK_FALSE(watch->take(changed));
    CHECK(changed.empty());
    BrushParams brush;
    brush.diameter = 4.0f;
    p.beginStroke(brush, { -40.5f, -40.5f, 1.0f });   // on the painted raster, in the master
    p.endStroke();
    CHECK_FALSE(watch->take(changed));
    CHECK(changed.contains(TileUtils::packKey(-1, -1)));
}

TEST_CASE("a change reaches everything downstream once, however many paths lead there")
{
    // 24 diamonds: one walk per path would be 2^24 of them.
    StarterWorkspace s;
    Workspace& p = s.workspace;
    const NodeId r = rasterWith(p, [](TileGrid& g) { fillPixel(g, 1, 1, RGBA{ 0, 0, 255, 255 }); });
    NodeId top = r;
    for (int d = 0; d < 24; ++d) {
        const NodeId a = p.addNode(NodeType::Reference, 0, 0, {}), b = p.addNode(NodeType::Reference, 0, 0, {});
        const NodeId c = p.addNode(NodeType::Compositor, 0, 0, {});
        REQUIRE(p.connect(top, a, InputSlot::input()));
        REQUIRE(p.connect(top, b, InputSlot::input()));
        REQUIRE(p.connect(a, c, InputSlot::newLayer(0)));
        REQUIRE(p.connect(b, c, InputSlot::newLayer(1)));
        top = c;
    }
    const std::shared_ptr<TileWatch> watch = p.find(top)->watchOutput();
    TileSet changed;
    watch->take(changed);   // a new reader reads everything once
    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE(p.setSignalScale(r, NodeSignal::Main, NodeChannel::A, 0.0f));
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5));
    CHECK_FALSE(watch->take(changed));
    CHECK(changed.contains(TileUtils::packKey(0, 0)));   // the bottom heard it
    CHECK(rendered(p, top, 1, 1) == transparent());
}

TEST_CASE("the order a change spreads in follows new links")
{
    StarterWorkspace s;
    Workspace& p = s.workspace;
    const NodeId r = rasterWith(p, [](TileGrid& g) { fillPixel(g, 0, 0, RGBA{ 9, 9, 9, 255 }); });
    const NodeId a = p.addNode(NodeType::Reference, 0, 0, {}), b = p.addNode(NodeType::Reference, 0, 0, {});
    REQUIRE(p.connect(r, a, InputSlot::input()));
    REQUIRE(p.setSignalScale(r, NodeSignal::Main, NodeChannel::A, 0.5f));   // the order downstream of r, worked out
    const std::shared_ptr<TileWatch> watch = p.find(b)->watchOutput();
    TileSet changed;
    watch->take(changed);
    REQUIRE(p.connect(a, b, InputSlot::input()));   // b is downstream now
    watch->take(changed);
    REQUIRE(p.setSignalScale(r, NodeSignal::Main, NodeChannel::A, 1.0f));
    CHECK_FALSE(watch->take(changed));
    CHECK(changed.contains(TileUtils::packKey(0, 0)));
    REQUIRE(p.disconnect(b, InputSlot::input()));   // and not any more
    watch->take(changed);
    REQUIRE(p.setSignalScale(r, NodeSignal::Main, NodeChannel::A, 0.5f));
    watch->take(changed);
    CHECK(changed.empty());
}
