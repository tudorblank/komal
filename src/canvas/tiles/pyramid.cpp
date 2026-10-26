#include "canvas/tiles/pyramid.hpp"

void TilePyramid::reset(Source source, TileSet footprint)
{
    m_source = std::move(source);
    for (Level& lv : m_levels) {
        lv.footprint.clear();
        lv.footprintReady = false;
        lv.cpu.clear();
        lv.current.clear();
    }
    m_levels[0].footprint      = std::move(footprint);
    m_levels[0].footprintReady = true;
}

const TileSet& TilePyramid::footprint(int level)
{
    Level& lv = m_levels[level];
    if (!lv.footprintReady) {
        for (TileKey k : m_levels[0].footprint) lv.footprint.insert(parentKey(k, level));
        lv.footprintReady = true;
    }
    return lv.footprint;
}

const Tile* TilePyramid::tile(const CanvasFrame& frame, int level, int tx, int ty, bool& complete)
{
    if (level == 0) return m_source(tx, ty);

    Level& lv = m_levels[level];
    const TileKey key = TileUtils::packKey(tx, ty);
    if (lv.current.contains(key)) return lv.cpu.getReadTile(tx, ty);

    // The four children first: each is kept at its level once built.
    const TileSet& below = footprint(level - 1);
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            const int cx = 2 * tx + i, cy = 2 * ty + j;
            if (level - 1 == 0 || !below.contains(TileUtils::packKey(cx, cy)) ||
                m_levels[level - 1].current.contains(TileUtils::packKey(cx, cy))) continue;
            if (!frame.inBudget()) { complete = false; return nullptr; }
            bool childComplete = true;
            tile(frame, level - 1, cx, cy, childComplete);
            if (!childComplete) { complete = false; return nullptr; }
        }
    if (level - 1 == 0 && !frame.inBudget()) { complete = false; return nullptr; }

    Tile& out = lv.cpu.writeOrCreateTile(tx, ty);
    out.clear();
    bool any = false;
    {
        Tile::BulkWrite w(out, Tile::BulkWrite::Trusted);
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                const int cx = 2 * tx + i, cy = 2 * ty + j;
                if (!below.contains(TileUtils::packKey(cx, cy))) continue;
                bool unused = true;
                if (const Tile* child = tile(frame, level - 1, cx, cy, unused)) {   // level 0: rendered now
                    downsampleQuadrant(w.data(), *child, i, j);
                    any = true;
                }
            }
        if (any) w.markChanged();
    }
    lv.current.insert(key);
    if (!any || out.isEmpty()) { lv.cpu.freeTile(tx, ty); return nullptr; }
    out.settle();
    return &out;
}

TileKey TilePyramid::parentKey(TileKey key, int levels)
{
    const auto t = TileUtils::unpackKey(key);
    return TileUtils::packKey(t.x >> levels, t.y >> levels);   // arithmetic shift: floor
}

void TilePyramid::downsampleQuadrant(RGBA* dst, const Tile& src, int qx, int qy)
{
    constexpr int N = Tile::kSize, H = N / 2;
    const RGBA* s = src.getReadAll();
    for (int y = 0; y < H; ++y) {
        const RGBA* r0 = s + size_t(2 * y) * N;
        const RGBA* r1 = r0 + N;
        RGBA* d = dst + size_t(qy * H + y) * N + qx * H;
        for (int x = 0; x < H; ++x) {
            const RGBA a = r0[2 * x], b = r0[2 * x + 1], c = r1[2 * x], e = r1[2 * x + 1];
            d[x] = { uint8_t((a.r + b.r + c.r + e.r + 2) >> 2), uint8_t((a.g + b.g + c.g + e.g + 2) >> 2),
                     uint8_t((a.b + b.b + c.b + e.b + 2) >> 2), uint8_t((a.a + b.a + c.a + e.a + 2) >> 2) };
        }
    }
}
