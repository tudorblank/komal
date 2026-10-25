#pragma once
// Pixels, bounds, tile coordinates, the Tile itself and per-tile edits.

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstring>

// === RGBA === //
// Premultiplied, canonical: rgb <= a, and a == 0 implies rgb == 0.
struct RGBA {
    uint8_t r, g, b, a;
    bool operator==(const RGBA&) const = default;
};
static_assert(sizeof(RGBA) == 4);

constexpr RGBA transparent() { return { 0, 0, 0, 0 }; }

constexpr RGBA canonical(RGBA c)
{ return { std::min(c.r, c.a), std::min(c.g, c.a), std::min(c.b, c.a), c.a }; }

constexpr uint8_t mul255(int v, int f) { return uint8_t((v * f + 127) / 255); }
constexpr RGBA scale(RGBA c, int f) { return { mul255(c.r, f), mul255(c.g, f), mul255(c.b, f), mul255(c.a, f) }; }

// s drawn on top of d. Both premultiplied + canonical; the result is too.
constexpr RGBA over(RGBA d, RGBA s)
{
    const int inv = 255 - s.a;
    auto ch = [&](int sc, int dc) { return uint8_t(std::min(255, sc + (dc * inv + 127) / 255)); };
    return { ch(s.r, d.r), ch(s.g, d.g), ch(s.b, d.b), ch(s.a, d.a) };
}

// === BOUNDS === //
// Inclusive integer rectangle; empty when min > max.
struct BoundsI {
    int minX = INT_MAX, minY = INT_MAX;   // up-left
    int maxX = INT_MIN, maxY = INT_MIN;   // down-right

    bool operator==(const BoundsI&) const = default;

    constexpr void expand(int x, int y)
    {
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    }
    constexpr void expand(const BoundsI& o) { if (!o.empty()) { expand(o.minX, o.minY); expand(o.maxX, o.maxY); } }
    constexpr void reset() { *this = BoundsI{}; }

    constexpr bool empty() const { return minX > maxX || minY > maxY; }
    constexpr bool contains(int x, int y) const { return x >= minX && x <= maxX && y >= minY && y <= maxY; }

    constexpr int width()  const { return empty() ? 0 : maxX - minX + 1; }
    constexpr int height() const { return empty() ? 0 : maxY - minY + 1; }

    constexpr BoundsI offsetBy(int dx, int dy) const
    { return empty() ? *this : BoundsI{ minX + dx, minY + dy, maxX + dx, maxY + dy }; }
    constexpr BoundsI intersect(const BoundsI& o) const
    { return { std::max(minX, o.minX), std::max(minY, o.minY), std::min(maxX, o.maxX), std::min(maxY, o.maxY) }; }
};

// === TILE COORDINATES === //
using TileKey = uint64_t;
using PageKey = uint64_t;

using TileSet = absl::flat_hash_set<TileKey>;
template<class V> using PageMap = absl::flat_hash_map<PageKey, V>;

namespace TileUtils {
    // world limit: keeps every coordinate computation far from int overflow
    constexpr int  kWorldLimit = 1 << 29;
    constexpr bool inWorld(int v) { return v >= -kWorldLimit && v <= kWorldLimit; }

    // tile
    constexpr int kTileShift = 6;
    constexpr int kTileDim   = 1 << kTileShift;   // 64
    constexpr int kTileMask  = kTileDim - 1;

    // page
    constexpr int kPageShift = 4;
    constexpr int kPageDim   = 1 << kPageShift;   // 16 tiles per page side
    constexpr int kPageMask  = kPageDim - 1;

    // key
    constexpr TileKey packKey(int x, int y) { return (uint64_t(uint32_t(x)) << 32) | uint32_t(y); }
    struct KeyXY { int32_t x, y; };
    constexpr KeyXY unpackKey(TileKey k) { return { int32_t(uint32_t(k >> 32)), int32_t(uint32_t(k)) }; }

    // coordinate conversion; >> on negatives is an arithmetic (floor) shift since C++20
    constexpr int worldToTile(int w)            { return w >> kTileShift; }
    constexpr int worldToLocal(int w)           { return w & kTileMask; }
    constexpr int tileToWorld(int t, int l = 0) { return t * kTileDim + l; }
    constexpr int tileToPage(int t)             { return t >> kPageShift; }
    constexpr int tileToPageLocal(int t)        { return t & kPageMask; }

    // does tile (tx, ty) touch the world rectangle b?
    constexpr bool tileOverlaps(const BoundsI& b, int tx, int ty)
    {
        const int x0 = tileToWorld(tx), y0 = tileToWorld(ty);
        return !b.empty() && x0 + kTileDim > b.minX && x0 <= b.maxX && y0 + kTileDim > b.minY && y0 <= b.maxY;
    }
} // namespace TileUtils
constexpr TileKey kNoKey = TileUtils::packKey(INT_MIN, INT_MIN);

struct TileStats {
    uint16_t count  = 0;   // pixels with a > 0
    uint16_t opaque = 0;   // pixels with a == 255
    BoundsI  bounds;       // tile-local, of the a > 0 pixels
    bool operator==(const TileStats&) const = default;
};

// === TILE === //
class Tile {
public:
    static constexpr int kSize   = TileUtils::kTileDim;
    static constexpr int kPixels = kSize * kSize;   // 4096

    // copies must be spelled out (illegal: tile = otherTile)
    Tile() = default;
    Tile(const Tile&) = delete;
    Tile& operator=(const Tile&) = delete;
    void copyFrom(const Tile& other)
    {
        std::memcpy(m_px, other.m_px, sizeof(m_px));
        m_stats      = other.m_stats;
        m_statsStale = other.m_statsStale;
    }

    // read
    const RGBA& getReadPx(int lx, int ly) const { assert(inTile(lx, ly)); return m_px[ly * kSize + lx]; }
    const RGBA* getReadAll() const { return m_px; }
    static constexpr bool inTile(int lx, int ly)
    { return unsigned(lx) < unsigned(kSize) && unsigned(ly) < unsigned(kSize); }

    // write (whole tile)
    RGBA* getWriteAll() { m_statsStale = true; return m_px; }
    void normalizeAll()
    {
        for (RGBA& p : m_px) p = canonical(p);
        m_statsStale = true;
    }
    void clear()
    {
        std::memset(m_px, 0, sizeof(m_px));
        m_stats      = {};
        m_statsStale = false;
    }

    // Bulk write without paying for a stats rescan when nothing was written: stats are only
    // invalidated if markChanged() was called. Untrusted data is made canonical on close.
    class BulkWrite {
    public:
        enum Trust { Trusted, Untrusted };
        BulkWrite(Tile& t, Trust trust) : m_t(t), m_trust(trust) {}
        ~BulkWrite()
        {
            if (!m_changed) return;
            if (m_trust == Untrusted) m_t.normalizeAll();
            m_t.m_statsStale = true;
        }
        BulkWrite(const BulkWrite&) = delete;
        BulkWrite& operator=(const BulkWrite&) = delete;

        RGBA* data() { return m_t.m_px; }
        void  markChanged() { m_changed = true; }

    private:
        Tile& m_t;
        Trust m_trust;
        bool  m_changed = false;
    };

    // write (per px)
    bool setPx(int lx, int ly, RGBA c)
    {
        assert(inTile(lx, ly));
        c = canonical(c);
        RGBA& px = m_px[ly * kSize + lx];
        if (px == c) return false;
        px = c;
        m_statsStale = true;
        return true;
    }
    bool erasePx(int lx, int ly) { return setPx(lx, ly, transparent()); }

    // stats: computed lazily on first read after a write. settle() computes them now, so later
    // const reads don't write (the tile can then be read from several threads).
    const TileStats& stats() const { if (m_statsStale) rescan(); return m_stats; }
    void settle() const { if (m_statsStale) rescan(); }

    const BoundsI& bounds() const { return stats().bounds; }
    int  count()            const { return stats().count; }
    int  opaqueCount()      const { return stats().opaque; }
    bool isEmpty()          const { return count() == 0; }
    bool isFull()           const { return count() == kPixels; }
    bool isFullyOpaque()    const { return opaqueCount() == kPixels; }

private:
    RGBA m_px[kPixels] = {};
    mutable TileStats m_stats;
    mutable bool m_statsStale = false;   // (was m_dirty: it means "stats out of date", not "needs upload")

    void rescan() const
    {
        TileStats s;
        for (int i = 0; i < kPixels; ++i) {
            if (m_px[i].a == 0) continue;
            ++s.count;
            if (m_px[i].a == 255) ++s.opaque;
            s.bounds.expand(i & TileUtils::kTileMask, i >> TileUtils::kTileShift);
        }
        m_stats = s;
        m_statsStale = false;
    }
};

// === TILE SPAN === //
// The part of one tile covered by a world rectangle.
struct TileSpan {
    int lx0, ly0, lx1, ly1;   // tile-local inclusive rect that is covered
    int tx, ty;               // tile coords
    int tileWX, tileWY;       // tile's world origin

    BoundsI worldBounds() const { return { tileWX + lx0, tileWY + ly0, tileWX + lx1, tileWY + ly1 }; }
    bool isWholeTile() const { return lx0 == 0 && ly0 == 0 && lx1 == Tile::kSize - 1 && ly1 == Tile::kSize - 1; }

    static TileSpan wholeTile(int tx, int ty)
    {
        return { .lx0 = 0, .ly0 = 0, .lx1 = Tile::kSize - 1, .ly1 = Tile::kSize - 1, .tx = tx, .ty = ty,
                 .tileWX = TileUtils::tileToWorld(tx), .tileWY = TileUtils::tileToWorld(ty) };
    }
};

template<class F>
inline void forEachTileSpan(const BoundsI& world, F&& f)
{
    if (world.empty()) return;
    for (int ty = TileUtils::worldToTile(world.minY); ty <= TileUtils::worldToTile(world.maxY); ++ty)
    for (int tx = TileUtils::worldToTile(world.minX); tx <= TileUtils::worldToTile(world.maxX); ++tx) {
        const int twx = TileUtils::tileToWorld(tx), twy = TileUtils::tileToWorld(ty);
        f(TileSpan{
            .lx0 = std::max(0, world.minX - twx),               .ly0 = std::max(0, world.minY - twy),
            .lx1 = std::min(Tile::kSize - 1, world.maxX - twx), .ly1 = std::min(Tile::kSize - 1, world.maxY - twy),
            .tx = tx, .ty = ty, .tileWX = twx, .tileWY = twy });
    }
}

// Copies the rows of span `s` of tile `src` into `dstPx` (a whole tile whose span is `d`),
// shifted by (dx, dy) in world space. The one row-copy shared by every gather.
inline void copySpanShifted(RGBA* dstPx, const Tile& src, const TileSpan& s, const TileSpan& d, int dx, int dy)
{
    const int dstX = s.tileWX + s.lx0 + dx - d.tileWX;
    for (int ly = s.ly0; ly <= s.ly1; ++ly) {
        const int dstY = s.tileWY + ly + dy - d.tileWY;
        std::memcpy(&dstPx[dstY * Tile::kSize + dstX], &src.getReadAll()[ly * Tile::kSize + s.lx0],
                    size_t(s.lx1 - s.lx0 + 1) * sizeof(RGBA));
    }
}

// === TILE EDITS === //
enum class CompositeMode : uint8_t { Over, Erase, Replace };

struct CompositeParams {
    uint8_t       opacity = 255;
    CompositeMode mode    = CompositeMode::Over;
};

namespace TileEdit {
    inline bool replace(Tile& dst, const Tile& src)
    {
        assert(&dst != &src);
        if (std::memcmp(dst.getReadAll(), src.getReadAll(), sizeof(RGBA) * Tile::kPixels) == 0) return false;
        dst.copyFrom(src);
        return true;
    }

    inline RGBA blendPixel(RGBA below, RGBA source, CompositeMode mode)
    {
        switch (mode) {
        case CompositeMode::Over:    return over(below, source);            // source drawn on top
        case CompositeMode::Erase:   return scale(below, 255 - source.a);   // source eats the destination
        case CompositeMode::Replace: break;                                 // handled per tile, never per pixel
        }
        assert(false && "Replace is not a per-pixel blend");
        return below;
    }

    inline bool composite(Tile& dst, const Tile& src, CompositeParams p)
    {
        assert(&dst != &src);
        assert(p.mode != CompositeMode::Replace);

        const BoundsI& area = src.bounds();
        if (area.empty()) return false;

        const RGBA* srcPixels = src.getReadAll();
        const RGBA* dstPixels = dst.getReadAll();
        RGBA* dstWritable = nullptr;   // taken on the first real change

        for (int y = area.minY; y <= area.maxY; ++y)
        for (int x = area.minX; x <= area.maxX; ++x) {
            const int i = y * Tile::kSize + x;
            RGBA source = srcPixels[i];
            if (p.opacity != 255) source = scale(source, p.opacity);
            if (source.a == 0) continue;

            const RGBA result = blendPixel(dstPixels[i], source, p.mode);
            if (result == dstPixels[i]) continue;

            if (!dstWritable) dstWritable = dst.getWriteAll();
            dstWritable[i] = result;
        }
        return dstWritable != nullptr;
    }

    inline bool clear(Tile& tile)
    {
        if (tile.isEmpty()) return false;
        tile.clear();   // also resets stats to "empty", which is exact
        return true;
    }
    inline bool clearRect(Tile& tile, const TileSpan& area)
    {
        const RGBA* pixels = tile.getReadAll();
        const int rowWidth = area.lx1 - area.lx0 + 1;

        bool hasContent = false;
        for (int y = area.ly0; y <= area.ly1 && !hasContent; ++y)
            for (int x = area.lx0; x <= area.lx1; ++x)
                if (pixels[y * Tile::kSize + x].a != 0) { hasContent = true; break; }
        if (!hasContent) return false;

        RGBA* writable = tile.getWriteAll();
        for (int y = area.ly0; y <= area.ly1; ++y)
            std::memset(&writable[y * Tile::kSize + area.lx0], 0, size_t(rowWidth) * sizeof(RGBA));
        return true;
    }
    inline bool fill(Tile& tile, RGBA color)
    {
        color = canonical(color);
        if (color.a == 0) return clear(tile);

        const RGBA* pixels = tile.getReadAll();
        int i = 0;
        while (i < Tile::kPixels && pixels[i] == color) ++i;
        if (i == Tile::kPixels) return false;

        RGBA* writable = tile.getWriteAll();
        std::fill(writable, writable + Tile::kPixels, color);
        return true;
    }
    inline bool apply(Tile& dst, const Tile& src, CompositeParams p)
    {
        assert(&dst != &src);
        if (p.mode == CompositeMode::Replace) return replace(dst, src);
        if (src.isEmpty()) return false;

        switch (p.mode) {
        case CompositeMode::Over:
            if (p.opacity == 255 && dst.isEmpty())       { dst.copyFrom(src); return true; }   // nothing underneath
            if (p.opacity == 255 && src.isFullyOpaque()) return replace(dst, src);             // nothing underneath can show
            return composite(dst, src, p);
        case CompositeMode::Erase:
            if (p.opacity == 255 && src.isFullyOpaque()) return clear(dst);                    // erases everything
            return composite(dst, src, p);
        case CompositeMode::Replace: break;
        }
        return false;
    }
} // namespace TileEdit
