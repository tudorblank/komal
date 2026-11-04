// The raster core on its own: pixels, tiles, grids, edits and their undo, brush strokes.

#include "raster/brush.hpp"
#include "raster/raster-data.hpp"

#include <doctest/doctest.h>
#include <random>
#include <vector>

namespace {

RGBA pixel(const RasterData& r, int x, int y)
{
    const Tile* t = r.getReadTile(TileUtils::worldToTile(x), TileUtils::worldToTile(y));
    return t ? t->getReadPx(TileUtils::worldToLocal(x), TileUtils::worldToLocal(y)) : transparent();
}

EditSummary edit(RasterData& r, const std::function<void(RasterEditor&)>& body)
{
    return r.edit(body);
}

}

TEST_CASE("pixels: premultiplied over, scale and canonical")
{
    const RGBA red{ 255, 0, 0, 255 }, halfBlue{ 0, 0, 128, 128 };
    CHECK(over(red, halfBlue) == RGBA{ 127, 0, 128, 255 });   // half the red shows through
    CHECK(over(halfBlue, transparent()) == halfBlue);           // nothing on top: unchanged
    CHECK(over(transparent(), red) == red);
    CHECK(scale(red, 128) == RGBA{ 128, 0, 0, 128 });
    CHECK(canonical(RGBA{ 200, 10, 90, 50 }) == RGBA{ 50, 10, 50, 50 });   // rgb never over alpha
    CHECK(canonical(RGBA{ 9, 9, 9, 0 }) == transparent());
}

TEST_CASE("tile edits: over, erase and replace, with opacity")
{
    Tile dst, src;
    TileEdit::fill(dst, RGBA{ 0, 0, 255, 255 });
    src.setPx(1, 1, RGBA{ 255, 0, 0, 255 });
    src.setPx(2, 1, RGBA{ 0, 128, 0, 128 });   // half transparent green (premultiplied)

    CHECK(TileEdit::apply(dst, src, { 255, CompositeMode::Over }));
    CHECK(dst.getReadPx(1, 1) == RGBA{ 255, 0, 0, 255 });
    CHECK(dst.getReadPx(2, 1) == RGBA{ 0, 128, 127, 255 });
    CHECK(dst.getReadPx(0, 0) == RGBA{ 0, 0, 255, 255 });     // where src is empty: untouched

    Tile erased;
    TileEdit::fill(erased, RGBA{ 200, 200, 200, 255 });
    CHECK(TileEdit::apply(erased, src, { 255, CompositeMode::Erase }));
    CHECK(erased.getReadPx(1, 1) == transparent());            // an opaque eraser takes it all
    CHECK(erased.getReadPx(2, 1).a == 127);                    // a half one, half

    Tile faint;
    CHECK(TileEdit::apply(faint, src, { 128, CompositeMode::Over }));   // half opacity onto nothing
    CHECK(faint.getReadPx(1, 1) == RGBA{ 128, 0, 0, 128 });

    Tile replaced;
    TileEdit::fill(replaced, RGBA{ 1, 2, 3, 255 });
    CHECK(TileEdit::apply(replaced, src, { 255, CompositeMode::Replace }));
    CHECK(replaced.getReadPx(0, 0) == transparent());
    CHECK_FALSE(TileEdit::apply(replaced, src, { 255, CompositeMode::Replace }));   // the same again: no change
}

TEST_CASE("tile stats follow every write")
{
    Tile t;
    CHECK(t.isEmpty());
    t.setPx(3, 7, RGBA{ 10, 10, 10, 255 });
    t.setPx(60, 2, RGBA{ 5, 5, 5, 100 });
    CHECK(t.count() == 2);
    CHECK(t.opaqueCount() == 1);
    CHECK(t.bounds() == BoundsI{ 3, 2, 60, 7 });
    t.erasePx(60, 2);
    CHECK(t.bounds() == BoundsI{ 3, 7, 3, 7 });
    CHECK(TileEdit::fill(t, RGBA{ 1, 1, 1, 255 }));
    CHECK(t.isFullyOpaque());
}

TEST_CASE("tile grid: tiles anywhere, pages that go when empty, readers across pages")
{
    TileGrid g;
    TileEdit::fill(g.writeOrCreateTile(-1, -1), RGBA{ 1, 0, 0, 255 });
    TileEdit::fill(g.writeOrCreateTile(16, 0), RGBA{ 0, 1, 0, 255 });   // another page
    TileEdit::fill(g.writeOrCreateTile(-17, 40), RGBA{ 0, 0, 1, 255 });
    CHECK(g.size() == 3);
    TileReader reader = g.reader();
    CHECK(reader.pixel(-1, -1).r == 1);
    CHECK(reader.pixel(16 * 64 + 5, 3).g == 1);
    CHECK(reader.pixel(-17 * 64, 40 * 64 + 63).b == 1);
    CHECK(reader.pixel(0, 0) == transparent());
    int visited = 0;
    g.forEachRead([&](int, int, const Tile&) { ++visited; });
    CHECK(visited == 3);
    CHECK(g.freeTile(16, 0));
    CHECK_FALSE(g.freeTile(16, 0));
    CHECK(g.size() == 2);
    CHECK(g.getReadTile(16, 0) == nullptr);
}

TEST_CASE("raster edits: bounds grow, shrink to what's left, and a snapshot restored brings both back")
{
    RasterData r(RasterId(0));
    CHECK(r.getBounds().empty());
    edit(r, [](RasterEditor& ed) {
        ed.setPixel(-5, 10, RGBA{ 255, 255, 255, 255 });
        ed.setPixel(200, -70, RGBA{ 255, 255, 255, 255 });
        ed.setPixel(30, 30, RGBA{ 255, 255, 255, 255 });
    });
    CHECK(r.getBounds() == BoundsI{ -5, -70, 200, 30 });
    const auto all = r.snapshot();

    // Erasing an extreme pixel shrinks the bounds to the pixels left (the recompute path).
    const EditSummary s = edit(r, [](RasterEditor& ed) { ed.erasePixel(200, -70); });
    CHECK(s.boundsChanged);
    CHECK(r.getBounds() == BoundsI{ -5, 10, 30, 30 });
    const auto erased = r.snapshot();
    // Erasing inside changes nothing at the edges.
    const uint64_t rev = r.revision();
    edit(r, [](RasterEditor& ed) { ed.setPixel(10, 20, RGBA{ 9, 9, 9, 255 }); });
    CHECK(r.getBounds() == BoundsI{ -5, 10, 30, 30 });
    CHECK(r.revision() == rev + 1);

    // An edit that changes nothing touches nothing and leaves the revision.
    CHECK(edit(r, [](RasterEditor& ed) { ed.erasePixel(1000, 1000); }).touched.empty());
    CHECK(r.revision() == rev + 1);

    // Back to before the erase: the corner comes back, bounds too; only the tiles that differ are told.
    const EditSummary back = r.restore(*all);
    CHECK(pixel(r, 200, -70).a == 255);
    CHECK(pixel(r, 10, 20).a == 0);
    CHECK(r.getBounds() == BoundsI{ -5, -70, 200, 30 });
    CHECK(back.boundsChanged);
    CHECK(back.touched.size() == 2);   // the corner's tile, and the tile painted after
    CHECK(r.restore(*all).touched.empty());   // already there: nothing
    r.restore(*erased);
    CHECK(pixel(r, 200, -70).a == 0);
    CHECK(r.getBounds() == BoundsI{ -5, 10, 30, 30 });

    // Clearing everything empties it; its snapshot from before brings every tile back.
    const auto beforeClear = r.snapshot();
    edit(r, [](RasterEditor& ed) { ed.clearAll(); });
    CHECK(r.getBounds().empty());
    CHECK(r.tileCount() == 0);
    r.restore(*beforeClear);
    CHECK(r.getBounds() == BoundsI{ -5, 10, 30, 30 });
    CHECK(pixel(r, 30, 30).a == 255);
}

TEST_CASE("history keeps cold what's far from it: snapshots' own tiles encoded, a little at a time")
{
    RasterData r(RasterId(0));
    std::vector<std::shared_ptr<const RasterSnapshot>> steps;
    for (int i = 0; i < 8; ++i) {   // eight snapshots, each with a few tiles of its own
        edit(r, [&](RasterEditor& ed) {
            for (int t = 0; t < 4; ++t)
                for (int y = 0; y < 64; y += 3) ed.setPixel(t * 64 + i, y, RGBA{ uint8_t(i * 30), 0, 0, 255 });
        });
        steps.push_back(r.snapshot());
    }
    const size_t hot = r.historyBytes();
    const RasterSnapshot* near[] = { steps[7].get() };   // history is at the last one
    while (!r.keepWarm(near, Deadline::in(Clock::Seconds(1.0)))) {}
    CHECK(r.historyBytes() < hot / 10);   // the others' tiles: their bytes only

    // Going back to a cold one: decoded, the same pixels.
    r.restore(*steps[2]);
    CHECK(pixel(r, 2, 0) == RGBA{ 60, 0, 0, 255 });
    CHECK(pixel(r, 7, 0).a == 0);
    const RasterSnapshot* nowNear[] = { steps[2].get(), steps[3].get() };
    while (!r.keepWarm(nowNear, Deadline::in(Clock::Seconds(1.0)))) {}
    r.restore(*steps[3]);   // warm already
    CHECK(pixel(r, 3, 3) == RGBA{ 90, 0, 0, 255 });
}

TEST_CASE("pasting with a shift: whole tiles, parts of tiles, negative ways")
{
    RasterData src(RasterId(0));
    edit(src, [](RasterEditor& ed) {
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 100; ++x)
                if ((x + y) % 7 == 0) ed.setPixel(x, y, RGBA{ uint8_t(x), uint8_t(y), 0, 255 });
    });
    for (const auto [dx, dy] : { std::pair{ 64, -128 }, std::pair{ -3, 70 }, std::pair{ -65, -1 } }) {
        RasterData dst(RasterId(1));
        edit(dst, [&](RasterEditor& ed) {
            TileReader reader = src.reader();
            ed.paste(reader, src.getBounds(), dx, dy, { 255, CompositeMode::Over });
        });
        CHECK(dst.getBounds() == src.getBounds().offsetBy(dx, dy));
        for (int y = 0; y < 100; y += 3)
            for (int x = 0; x < 100; x += 5) CHECK(pixel(dst, x + dx, y + dy) == pixel(src, x, y));
    }
}

TEST_CASE("an image placed anywhere: tiles where it lands, none where it's empty")
{
    // 3 x 2 pixels, premultiplied bytes: an opaque one, a transparent one, a half one.
    const std::vector<uint8_t> rgba = { 255, 0, 0, 255,   0, 0, 0, 0,   64, 64, 64, 128,
                                        0, 0, 0, 0,       0, 0, 0, 0,   0, 255, 0, 255 };
    TileGrid tiles;
    tilesFromImage(tiles, { rgba, 3, 2 }, -65, 63);   // across four tiles
    TileReader r = tiles.reader();
    CHECK(r.pixel(-65, 63) == RGBA{ 255, 0, 0, 255 });
    CHECK(r.pixel(-63, 63) == RGBA{ 64, 64, 64, 128 });
    CHECK(r.pixel(-63, 64) == RGBA{ 0, 255, 0, 255 });
    CHECK(tiles.getReadTile(-2, 1) == nullptr);   // (-65, 64): only a transparent pixel landed there
    CHECK(tiles.size() == 3);
}

TEST_CASE("a stroke's cover bounds are what it composes over nothing")
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (int round = 0; round < 40; ++round) {
        BrushParams p;
        p.diameter  = 1.0f + unit(rng) * 30.0f;
        p.hardness  = unit(rng);
        p.flow      = 0.05f + unit(rng) * 0.95f;
        p.antialias = unit(rng) > 0.3f;
        p.opacity   = uint8_t(1 + int(unit(rng) * 254.0f));
        p.color     = { 200, 100, 50, uint8_t(1 + int(unit(rng) * 254.0f)) };
        BrushStroke stroke;
        stroke.begin(p, { unit(rng) * 128.0f - 64.0f, unit(rng) * 128.0f - 64.0f, 1.0f });
        stroke.moveTo({ unit(rng) * 128.0f - 64.0f, unit(rng) * 128.0f - 64.0f, unit(rng) });
        for (const TileKey key : stroke.takeDirty()) {
            const auto [tx, ty] = TileUtils::unpackKey(key);
            Tile composed;
            const bool any = stroke.composeTile(composed, nullptr, tx, ty);
            CHECK(stroke.coverBounds(tx, ty) == (any ? composed.bounds() : BoundsI{}));
        }
    }
}

TEST_CASE("alpha lock: a stroke recolours, never changes alpha")
{
    Tile original;
    original.setPx(5, 5, RGBA{ 0, 0, 100, 100 });   // faint blue
    BrushParams p;
    p.diameter  = 40.0f;
    p.color     = { 255, 0, 0, 255 };
    p.keepAlpha = true;
    BrushStroke stroke;
    stroke.begin(p, { 5.5f, 5.5f, 1.0f });
    Tile out;
    REQUIRE(stroke.composeTile(out, &original, 0, 0));
    CHECK(out.getReadPx(5, 5).a == 100);
    CHECK(out.getReadPx(5, 5).r == 100);   // red now, at the same alpha
    CHECK(out.getReadPx(6, 6) == transparent());   // nothing there before: nothing now
}

TEST_CASE("a grid's copy is a snapshot: shared until either writes, then only what's touched is copied")
{
    TileGrid live;
    for (int i = 0; i < 40; ++i) TileEdit::fill(live.writeOrCreateTile(i, 0), RGBA{ 1, 2, 3, 255 });
    TilePool& pool = live.pool();
    CHECK(pool.liveCount() == 40);

    const TileGrid before = live;                 // shares every page and tile
    CHECK(pool.liveCount() == 40);
    CHECK(before.size() == 40);
    TileEdit::fill(live.writeOrCreateTile(5, 0), RGBA{ 9, 9, 9, 255 });   // that one tile copied
    CHECK(pool.liveCount() == 41);
    CHECK(before.getReadTile(5, 0)->getReadPx(0, 0) == RGBA{ 1, 2, 3, 255 });   // the snapshot as it was
    CHECK(live.getReadTile(5, 0)->getReadPx(0, 0) == RGBA{ 9, 9, 9, 255 });
    CHECK(before.getReadTile(6, 0) == live.getReadTile(6, 0));               // the rest: the very same tile

    std::vector<std::pair<int, int>> different;
    live.forEachDifferent(before, [&](int tx, int ty) { different.push_back({ tx, ty }); });
    CHECK(different == std::vector<std::pair<int, int>>{ { 5, 0 } });

    CHECK(live.freeTile(7, 0));                   // gone from the live grid only
    CHECK(before.getReadTile(7, 0));
    CHECK(pool.liveCount() == 41);                // the snapshot still holds it
    live.writeOrCreateTile(100, 0);
    TileGrid empty;
    {
        TileGrid gone = before;                   // another snapshot, let go of
        TileEdit::fill(gone.writeOrCreateTile(1, 0), RGBA{ 4, 4, 4, 255 });
    }
    CHECK(before.getReadTile(1, 0)->getReadPx(0, 0) == RGBA{ 1, 2, 3, 255 });

    // A grid moved from is an empty one, ready to use.
    TileGrid moved = std::move(empty);
    TileEdit::fill(empty.writeOrCreateTile(0, 0), RGBA{ 7, 7, 7, 255 });
    CHECK(empty.size() == 1);
    CHECK(moved.size() == 0);
}

TEST_CASE("tiles go cold (their bytes) and warm again (decoded), encoded once; the last holder frees them")
{
    TilePool pool;
    const TileId id = pool.allocate();
    Tile& t = pool.getWrite(id);
    for (int i = 0; i < 64; ++i) t.setPx(i, i, RGBA{ uint8_t(i), 0, uint8_t(i), 255 });
    t.settle();
    CHECK(pool.rawBytes() == sizeof(Tile));

    pool.cool(id);
    CHECK_FALSE(pool.isRaw(id));
    CHECK(pool.rawBytes() == 0);
    CHECK(pool.coldBytes() > 0);
    CHECK(pool.coldBytes() < sizeof(Tile) / 10);
    const size_t bytes = pool.coldBytes();
    CHECK(pool.getRead(id).getReadPx(40, 40) == RGBA{ 40, 0, 40, 255 });   // raw again, the same pixels
    CHECK(pool.isRaw(id));
    pool.cool(id);                                                         // its bytes kept: not encoded again
    CHECK(pool.coldBytes() == bytes);
    pool.getWrite(id).setPx(0, 1, RGBA{ 5, 5, 5, 255 });                  // written: its bytes are stale, gone
    CHECK(pool.coldBytes() == 0);

    pool.hold(id);                                                         // two holders
    pool.release(id);
    CHECK(pool.liveCount() == 1);
    pool.release(id);                                                      // the last one: freed
    CHECK(pool.liveCount() == 0);
    CHECK(pool.rawBytes() == 0);
}
