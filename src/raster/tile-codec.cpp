#include "raster/tile-codec.hpp"

#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {

enum class Form : uint8_t { Empty = 0, Solid = 1, Packed = 2, Raw = 3 };
enum class Filter : uint8_t { None = 0, Sub = 1, Up = 2, Average = 3, Paeth = 4 };

constexpr int    kBpp      = int(sizeof(RGBA));          // bytes per pixel, the filters' step
constexpr size_t kRowBytes = size_t(Tile::kSize) * kBpp;
constexpr size_t kRawBytes = kRowBytes * Tile::kSize;   // 16 KB
constexpr size_t kFiltered = (kRowBytes + 1) * Tile::kSize;   // each row: its filter, its bytes
constexpr int    kLevel    = 3;                          // zstd's default: fast both ways, small

struct CCtxFree { void operator()(ZSTD_CCtx* c) const { ZSTD_freeCCtx(c); } };
struct DCtxFree { void operator()(ZSTD_DCtx* c) const { ZSTD_freeDCtx(c); } };
using Compressor   = std::unique_ptr<ZSTD_CCtx, CCtxFree>;
using Decompressor = std::unique_ptr<ZSTD_DCtx, DCtxFree>;

uint8_t paeth(int a, int b, int c)
{
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return uint8_t(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

// What `filter` predicts for byte i of a row, from the row (as it is, unfiltered) and the one above.
uint8_t predict(Filter filter, const uint8_t* row, const uint8_t* above, size_t i)
{
    const int a = i >= size_t(kBpp) ? row[i - kBpp] : 0;
    const int b = above[i];
    const int c = i >= size_t(kBpp) ? above[i - kBpp] : 0;
    switch (filter) {
    case Filter::None:    return 0;
    case Filter::Sub:     return uint8_t(a);
    case Filter::Up:      return uint8_t(b);
    case Filter::Average: return uint8_t((a + b) / 2);
    case Filter::Paeth:   return paeth(a, b, c);
    }
    return 0;
}

// The rows, each through the filter that leaves it smallest (least sum of |byte| as signed): PNG's heuristic.
void filterRows(const uint8_t* px, uint8_t* out)
{
    static const uint8_t kZeros[kRowBytes] = {};
    for (int y = 0; y < Tile::kSize; ++y) {
        const uint8_t* row = px + size_t(y) * kRowBytes;
        const uint8_t* above = y > 0 ? row - kRowBytes : kZeros;
        uint8_t* dst = out + size_t(y) * (kRowBytes + 1);
        Filter best = Filter::None;
        long bestCost = -1;
        for (const Filter f : { Filter::None, Filter::Sub, Filter::Up, Filter::Average, Filter::Paeth }) {
            long cost = 0;
            for (size_t i = 0; i < kRowBytes; ++i) cost += std::abs(int(int8_t(uint8_t(row[i] - predict(f, row, above, i)))));
            if (bestCost < 0 || cost < bestCost) {
                bestCost = cost;
                best = f;
            }
        }
        dst[0] = uint8_t(best);
        for (size_t i = 0; i < kRowBytes; ++i) dst[1 + i] = uint8_t(row[i] - predict(best, row, above, i));
    }
}

bool unfilterRows(const uint8_t* in, uint8_t* px)
{
    static const uint8_t kZeros[kRowBytes] = {};
    for (int y = 0; y < Tile::kSize; ++y) {
        const uint8_t* src = in + size_t(y) * (kRowBytes + 1);
        uint8_t* row = px + size_t(y) * kRowBytes;
        const uint8_t* above = y > 0 ? row - kRowBytes : kZeros;
        if (src[0] > uint8_t(Filter::Paeth)) return false;
        const Filter f = Filter(src[0]);
        for (size_t i = 0; i < kRowBytes; ++i) row[i] = uint8_t(src[1 + i] + predict(f, row, above, i));   // left of i: done already
    }
    return true;
}

// A tile coordinate inside the world, checked as one (its pixel coordinate could overflow).
bool tileInWorld(int t) { return t >= -(TileUtils::kWorldLimit >> TileUtils::kTileShift) && t <= (TileUtils::kWorldLimit >> TileUtils::kTileShift); }

const uint8_t* bytesOf(const Tile& t) { return reinterpret_cast<const uint8_t*>(t.getReadAll()); }

void encodeWith(ZSTD_CCtx* cctx, const Tile& tile, ByteWriter& out)
{
    tile.settle();
    if (tile.isEmpty()) {
        out.u8(uint8_t(Form::Empty));
        return;
    }
    const RGBA* px = tile.getReadAll();
    if (std::all_of(px, px + Tile::kPixels, [&](const RGBA& p) { return p == px[0]; })) {
        out.u8(uint8_t(Form::Solid));
        out.raw({ reinterpret_cast<const uint8_t*>(px), sizeof(RGBA) });
        return;
    }
    std::array<uint8_t, kFiltered> filtered;
    filterRows(bytesOf(tile), filtered.data());
    std::vector<uint8_t> packed(ZSTD_compressBound(kFiltered));
    const size_t n = ZSTD_compressCCtx(cctx, packed.data(), packed.size(), filtered.data(), filtered.size(), kLevel);
    if (!ZSTD_isError(n) && n + 4 < kRawBytes) {
        out.u8(uint8_t(Form::Packed));
        out.u32(uint32_t(n));
        out.raw({ packed.data(), n });
        return;
    }
    out.u8(uint8_t(Form::Raw));
    out.raw({ bytesOf(tile), kRawBytes });
}

bool decodeWith(ZSTD_DCtx* dctx, ByteReader& in, Tile& out)
{
    const auto put = [&](const uint8_t* bytes) {
        Tile::BulkWrite w(out, Tile::BulkWrite::Untrusted);   // from outside: made canonical
        std::memcpy(w.data(), bytes, kRawBytes);
        w.markChanged();
    };
    switch (Form(in.u8())) {
    case Form::Empty: {
        static const uint8_t kNothing[kRawBytes] = {};
        put(kNothing);
        break;
    }
    case Form::Solid: {
        const std::span<const uint8_t> c = in.raw(sizeof(RGBA));
        if (!in.ok()) return false;
        Tile::BulkWrite w(out, Tile::BulkWrite::Untrusted);
        RGBA colour;
        std::memcpy(&colour, c.data(), sizeof colour);
        std::fill(w.data(), w.data() + Tile::kPixels, colour);
        w.markChanged();
        break;
    }
    case Form::Packed: {
        const uint32_t n = in.u32();
        if (!in.ok() || n > ZSTD_compressBound(kFiltered)) return false;
        const std::span<const uint8_t> packed = in.raw(n);
        if (!in.ok()) return false;
        std::array<uint8_t, kFiltered> filtered;
        const size_t got = ZSTD_decompressDCtx(dctx, filtered.data(), filtered.size(), packed.data(), packed.size());
        if (ZSTD_isError(got) || got != kFiltered) return false;
        std::array<uint8_t, kRawBytes> px;
        if (!unfilterRows(filtered.data(), px.data())) return false;
        put(px.data());
        break;
    }
    case Form::Raw: {
        const std::span<const uint8_t> px = in.raw(kRawBytes);
        if (!in.ok()) return false;
        put(px.data());
        break;
    }
    default:
        return false;
    }
    out.settle();
    return in.ok();
}

}

void encodeTile(const Tile& tile, ByteWriter& out)
{
    const Compressor cctx(ZSTD_createCCtx());
    encodeWith(cctx.get(), tile, out);
}

bool decodeTile(ByteReader& in, Tile& out)
{
    const Decompressor dctx(ZSTD_createDCtx());
    return decodeWith(dctx.get(), in, out);
}

void encodeTiles(const TileGrid& tiles, ByteWriter& out)
{
    struct At { int tx, ty; const Tile* tile; };
    std::vector<At> all;
    all.reserve(tiles.size());
    tiles.forEachRead([&](int tx, int ty, const Tile& t) { all.push_back({ tx, ty, &t }); });
    std::sort(all.begin(), all.end(), [](const At& a, const At& b) { return a.ty != b.ty ? a.ty < b.ty : a.tx < b.tx; });
    const Compressor cctx(ZSTD_createCCtx());   // one for the whole set
    out.u32(uint32_t(all.size()));
    for (const At& a : all) {
        out.i32(a.tx);
        out.i32(a.ty);
        encodeWith(cctx.get(), *a.tile, out);
    }
}

bool decodeTiles(ByteReader& in, TileGrid& out)
{
    const uint32_t count = in.u32();
    if (!in.ok() || count > in.left() / 9) return false;   // each one: its coordinates and at least its form
    const Decompressor dctx(ZSTD_createDCtx());
    for (uint32_t i = 0; i < count; ++i) {
        const int tx = in.i32(), ty = in.i32();
        if (!in.ok() || !tileInWorld(tx) || !tileInWorld(ty)) return false;
        if (out.getReadTile(tx, ty)) return false;   // twice
        Tile& tile = out.writeOrCreateTile(tx, ty);
        if (!decodeWith(dctx.get(), in, tile)) return false;
        if (tile.isEmpty()) out.freeTile(tx, ty);
    }
    return in.ok();
}
