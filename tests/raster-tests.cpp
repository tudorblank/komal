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
    EditSummary s;
    (void)r.recordEdit(body, s);
    return s;
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

TEST_CASE("raster edits: bounds grow, shrink to what's left, and undo flips both")
{
    RasterData r(RasterId(0));
    CHECK(r.getBounds().empty());
    edit(r, [](RasterEditor& ed) {
        ed.setPixel(-5, 10, RGBA{ 255, 255, 255, 255 });
        ed.setPixel(200, -70, RGBA{ 255, 255, 255, 255 });
        ed.setPixel(30, 30, RGBA{ 255, 255, 255, 255 });
    });
    CHECK(r.getBounds() == BoundsI{ -5, -70, 200, 30 });

    // Erasing an extreme pixel shrinks the bounds to the pixels left (the recompute path).
    EditSummary s;
    const RasterCommitId erase = r.recordEdit([](RasterEditor& ed) { ed.erasePixel(200, -70); }, s);
    CHECK(s.boundsChanged);
    CHECK(r.getBounds() == BoundsI{ -5, 10, 30, 30 });
    // Erasing inside changes nothing at the edges.
    const uint64_t rev = r.revision();
    edit(r, [](RasterEditor& ed) { ed.setPixel(10, 20, RGBA{ 9, 9, 9, 255 }); });
    CHECK(r.getBounds() == BoundsI{ -5, 10, 30, 30 });
    CHECK(r.revision() == rev + 1);

    // An edit that changes nothing leaves no commit and no revision.
    EditSummary none;
    CHECK(r.recordEdit([](RasterEditor& ed) { ed.erasePixel(1000, 1000); }, none) == kNoRasterCommit);
    CHECK(r.revision() == rev + 1);

    // Undo the erase: the corner comes back, bounds too; redo takes it again.
    r.flip(erase);
    CHECK(pixel(r, 200, -70).a == 255);
    CHECK(r.getBounds() == BoundsI{ -5, -70, 200, 30 });
    r.flip(erase);
    CHECK(pixel(r, 200, -70).a == 0);
    CHECK(r.getBounds() == BoundsI{ -5, 10, 30, 30 });

    // Clearing everything empties it; its undo brings every tile back.
    const RasterCommitId cleared = r.recordEdit([](RasterEditor& ed) { ed.clearAll(); }, s);
    CHECK(r.getBounds().empty());
    CHECK(r.tileCount() == 0);
    r.flip(cleared);
    CHECK(r.getBounds() == BoundsI{ -5, 10, 30, 30 });
    CHECK(pixel(r, 10, 20).r == 9);
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

TEST_CASE("a tile pool gives a block back once all its tiles are free, and makes it again")
{
    TileGrid g;
    for (int i = 0; i < 40; ++i) TileEdit::fill(g.writeOrCreateTile(i, 0), RGBA{ 1, 2, 3, 255 });
    CHECK(g.bytes() == 48 * sizeof(Tile));   // three blocks of 16
    for (int i = 0; i < 16; ++i) CHECK(g.freeTile(i, 0));
    CHECK(g.bytes() == 32 * sizeof(Tile));   // the first block's tiles all went: so did it
    for (int i = 16; i < 40; ++i) CHECK(g.getReadTile(i, 0)->getReadPx(0, 0) == RGBA{ 1, 2, 3, 255 });   // the rest untouched
    TileEdit::fill(g.writeOrCreateTile(100, 0), RGBA{ 9, 9, 9, 255 });   // reuses a freed id: its block again
    CHECK(g.bytes() == 48 * sizeof(Tile));
    CHECK(g.getReadTile(100, 0)->getReadPx(5, 5) == RGBA{ 9, 9, 9, 255 });
    CHECK(g.size() == 25);
}
