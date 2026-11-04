// Tiles as binary (raster/tile-codec): each tile in its smallest form, read back to the byte; a
// set with each tile's coordinates, the same bytes for the same tiles; damaged data refused, never
// trusted.

#include "helpers.hpp"
#include "base/bytes.hpp"
#include "raster/tile-codec.hpp"

#include <cstdio>
#include <random>

namespace {

constexpr size_t kRawBytes = sizeof(RGBA) * Tile::kPixels;

std::vector<uint8_t> encoded(const Tile& t)
{
    ByteWriter w;
    encodeTile(t, w);
    return w.take();
}

void paint(Tile& t, const std::function<RGBA(int x, int y)>& f)
{
    for (int y = 0; y < Tile::kSize; ++y)
        for (int x = 0; x < Tile::kSize; ++x) t.setPx(x, y, canonical(f(x, y)));
    t.settle();
}

bool same(const Tile& a, const Tile& b)
{
    return std::memcmp(a.getReadAll(), b.getReadAll(), kRawBytes) == 0;
}

void roundTrip(const Tile& t)
{
    const std::vector<uint8_t> bytes = encoded(t);
    ByteReader r(bytes);
    Tile back;
    REQUIRE(decodeTile(r, back));
    CHECK(r.atEnd());
    CHECK(same(t, back));
}

}

TEST_CASE("a tile in its smallest form: empty, solid, packed, raw; read back to the byte")
{
    Tile empty;
    CHECK(encoded(empty).size() == 1);
    roundTrip(empty);

    Tile solid;
    paint(solid, [](int, int) { return RGBA{ 40, 80, 120, 200 }; });
    CHECK(encoded(solid).size() == 5);
    roundTrip(solid);

    Tile gradient;   // smooth: the filters make it almost nothing
    paint(gradient, [](int x, int y) { return RGBA{ uint8_t(x * 4), uint8_t(y * 4), uint8_t((x + y) * 2), 255 }; });
    const size_t g = encoded(gradient).size();
    CHECK(g < kRawBytes / 20);
    roundTrip(gradient);

    Tile stroke;     // a brush disc's edge: mostly empty, a soft rim
    paint(stroke, [](int x, int y) {
        const float d = std::sqrt(float((x - 10) * (x - 10) + (y - 50) * (y - 50)));
        const uint8_t a = d < 30.0f ? 255 : d < 38.0f ? uint8_t(255.0f * (38.0f - d) / 8.0f) : 0;
        return RGBA{ a, uint8_t(a / 2), 0, a };
    });
    const size_t s = encoded(stroke).size();
    CHECK(s < kRawBytes / 10);
    roundTrip(stroke);

    Tile noise;      // nothing to predict: never bigger than raw (one byte for its form)
    std::mt19937 rng(7);
    paint(noise, [&](int, int) { return RGBA{ uint8_t(rng()), uint8_t(rng()), uint8_t(rng()), uint8_t(rng()) }; });
    CHECK(encoded(noise).size() <= 1 + kRawBytes);
    roundTrip(noise);
    std::printf("tile sizes, raw %zu: gradient %zu, stroke %zu\n", kRawBytes, g, s);
}

TEST_CASE("a set of tiles: coordinates and content, the same bytes for the same tiles")
{
    TileGrid a, b;
    const int at[][2] = { { 0, 0 }, { -1, 3 }, { 5, -2 }, { 1000, -1000 } };
    for (const auto& p : at) fillPixel(a, p[0] * Tile::kSize + 3, p[1] * Tile::kSize + 9, RGBA{ 9, 8, 7, 200 });
    for (int i = 3; i >= 0; --i) fillPixel(b, at[i][0] * Tile::kSize + 3, at[i][1] * Tile::kSize + 9, RGBA{ 9, 8, 7, 200 });   // made the other way round
    ByteWriter wa, wb;
    encodeTiles(a, wa);
    encodeTiles(b, wb);
    CHECK(wa.bytes() == wb.bytes());

    ByteReader r(wa.bytes());
    TileGrid back;
    REQUIRE(decodeTiles(r, back));
    CHECK(r.atEnd());
    CHECK(back.size() == 4);
    for (const auto& p : at) {
        const Tile* t = back.getReadTile(p[0], p[1]);
        REQUIRE(t);
        CHECK(t->getReadPx(3, 9) == RGBA{ 9, 8, 7, 200 });
        CHECK(t->count() == 1);
    }
}

TEST_CASE("damaged tiles are refused, never trusted: cut short, twice, out of the world, garbage")
{
    TileGrid grid;
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; x += 2) fillPixel(grid, x, y, RGBA{ uint8_t(x), uint8_t(y), 0, 255 });
    ByteWriter w;
    encodeTiles(grid, w);
    const std::vector<uint8_t> bytes = w.take();

    for (size_t n = 0; n < bytes.size(); n += 1 + n / 11) {   // cut anywhere
        CAPTURE(n);
        ByteReader r(std::span<const uint8_t>(bytes).first(n));
        TileGrid back;
        CHECK_FALSE(decodeTiles(r, back));
    }

    ByteWriter twice;
    twice.u32(2);
    for (int i = 0; i < 2; ++i) {
        twice.i32(4);
        twice.i32(4);
        twice.u8(1);
        twice.raw(std::vector<uint8_t>{ 1, 1, 1, 1 });
    }
    ByteReader rt(twice.bytes());
    TileGrid t;
    CHECK_FALSE(decodeTiles(rt, t));

    ByteWriter far;
    far.u32(1);
    far.i32(1 << 28);   // past the world's edge
    far.i32(0);
    far.u8(0);
    ByteReader rf(far.bytes());
    TileGrid f;
    CHECK_FALSE(decodeTiles(rf, f));

    // Bytes flipped at random: refused, or read as canonical pixels; never a crash.
    std::mt19937 rng(11);
    for (int round = 0; round < 200; ++round) {
        std::vector<uint8_t> broken = bytes;
        for (int k = 0; k < 4; ++k) broken[rng() % broken.size()] ^= uint8_t(1 + rng() % 255);
        ByteReader r(broken);
        TileGrid back;
        if (!decodeTiles(r, back)) continue;
        back.forEachRead([&](int, int, const Tile& tile) {
            for (int i = 0; i < Tile::kPixels; ++i) {
                const RGBA p = tile.getReadAll()[i];
                CHECK((p.r <= p.a && p.g <= p.a && p.b <= p.a));
            }
        });
    }
}
