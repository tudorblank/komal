#include "raster.hpp"
#include "canvas/tiles/pyramid.hpp"
#include "core/perf.hpp"
#include "node/node-types.hpp"

#include <algorithm>
#include <cmath>

namespace {

int worldToTileClamped(float w)
{
    return TileUtils::worldToTile((int)std::clamp(std::floor(w), (float)-TileUtils::kWorldLimit, (float)TileUtils::kWorldLimit));
}

} // namespace

void RasterItem::setSource(CompositorNode* source)
{
    if (source == m_source) return;
    m_source = source;
    m_fullSync = true;
    update();
}

void RasterItem::setOpacity(float opacity)
{
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (opacity == m_opacity) return;
    m_opacity = opacity;
    m_instancesDirty = true;
    update();
}

void RasterItem::setSuppressed(bool suppressed)
{
    if (suppressed == m_suppressed) return;
    m_suppressed = suppressed;
    update();
}

size_t RasterItem::chunkCount() const
{
    size_t n = 0;
    for (const Level& lv : m_levels) n += lv.atlas.tileCount();
    return n;
}

size_t RasterItem::pageCount() const
{
    size_t n = 0;
    for (const Level& lv : m_levels) n += lv.atlas.pageCount();
    return n;
}

// ---------------------------------------------------------------- source -> levels

void RasterItem::catchUpSource()
{
    if (!m_source) {
        for (Level& lv : m_levels) {
            lv.atlas.clear();
            lv.footprint.clear(); lv.current.clear(); lv.cpu.clear(); lv.cpuCurrent.clear();
        }
        m_instancesDirty = true;
        return;
    }
    if (m_source->takeFullSync() || m_fullSync) {   // start over from what the output can cover
        m_fullSync = false;
        const TileSet fp = m_source->footprint();
        for (int l = 0; l < kLevels; ++l) {
            Level& lv = m_levels[l];
            lv.atlas.clear();
            lv.current.clear(); lv.cpu.clear(); lv.cpuCurrent.clear();
            lv.footprint.clear();
            for (TileKey k : fp) lv.footprint.insert(TilePyramid::parentKey(k, l));
        }
        m_source->drainDirtyTiles();   // covered by starting over
        m_instancesDirty = true;
        m_computeDirty = true;
        return;
    }
    for (TileKey key : m_source->drainDirtyTiles()) invalidate(key);
}

void RasterItem::invalidate(TileKey key)
{
    for (int l = 0; l < kLevels; ++l) {
        Level& lv = m_levels[l];
        const TileKey k = TilePyramid::parentKey(key, l);
        lv.current.erase(k);   // the atlas keeps the old picture until the new one is there
        m_computeDirty = true;
        lv.footprint.insert(k);
        if (l > 0) lv.cpuCurrent.erase(k);
    }
    m_instancesDirty = true;
}

RasterItem::Range RasterItem::rangeAt(const CanvasFrame& frame, int level, int margin) const
{
    return { (worldToTileClamped(frame.view.minX) >> level) - margin, (worldToTileClamped(frame.view.minY) >> level) - margin,
             (worldToTileClamped(frame.view.maxX) >> level) + margin, (worldToTileClamped(frame.view.maxY) >> level) + margin };
}

const Tile* RasterItem::lodTile(CanvasFrame& frame, int level, int tx, int ty, bool& complete)
{
    if (level == 0) return renderTile(*m_source, tx, ty, m_render);   // the compositor caches it

    Level& lv = m_levels[level];
    const TileKey key = TileUtils::packKey(tx, ty);
    if (lv.cpuCurrent.contains(key)) return lv.cpu.getReadTile(tx, ty);

    // The four children first: each is kept at its level once done, so running out of budget
    // half-way loses nothing.
    const Level& below = m_levels[level - 1];
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            const int cx = 2 * tx + i, cy = 2 * ty + j;
            if (!below.footprint.contains(TileUtils::packKey(cx, cy))) continue;
            if (level - 1 > 0 && !below.cpuCurrent.contains(TileUtils::packKey(cx, cy))) {
                if (!frame.inBudget()) { complete = false; return nullptr; }
                bool childComplete = true;
                lodTile(frame, level - 1, cx, cy, childComplete);
                if (!childComplete) { complete = false; return nullptr; }
            }
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
                if (!below.footprint.contains(TileUtils::packKey(cx, cy))) continue;
                bool unused = true;
                if (const Tile* child = lodTile(frame, level - 1, cx, cy, unused)) {
                    TilePyramid::downsampleQuadrant(w.data(), *child, i, j);
                    any = true;
                }
            }
        if (any) w.markChanged();
    }
    lv.cpuCurrent.insert(key);
    if (!any || out.isEmpty()) { lv.cpu.freeTile(tx, ty); return nullptr; }
    out.settle();
    return &out;
}

bool RasterItem::computeVisible(CanvasFrame& frame, const Range& visible, const Range& margin)
{
    PerfScope perf(Perf::CanvasSync);
    Level& lv = m_levels[m_drawLevel];
    bool changed = false;
    bool done = true;   // every tile of both ranges is current
    m_visiblePending = false;

    // Strictly visible tiles first, then the margin around them (ready before a pan shows them).
    for (int pass = 0; pass < 2; ++pass) {
        const Range& r = pass == 0 ? visible : margin;
        const auto visit = [&](TileKey key) {
            if (lv.current.contains(key)) return;
            const auto t = TileUtils::unpackKey(key);
            if (pass == 1 && t.x >= visible.minX && t.x <= visible.maxX && t.y >= visible.minY && t.y <= visible.maxY) return;
            bool complete = frame.inBudget();
            const Tile* tile = complete ? lodTile(frame, m_drawLevel, t.x, t.y, complete) : nullptr;
            if (!complete) {
                if (pass == 0) m_visiblePending = true;
                done = false;
                return;
            }
            if (tile) lv.atlas.upload(frame.pipelines, key, tile->getReadAll());
            else      lv.atlas.free(key);
            lv.current.insert(key);
            changed = true;
        };
        const double rangeTiles = double(r.maxX - r.minX + 1) * double(r.maxY - r.minY + 1);
        if (double(lv.footprint.size()) <= rangeTiles) {   // walk whichever is smaller
            for (TileKey key : lv.footprint) {
                const auto t = TileUtils::unpackKey(key);
                if (t.x >= r.minX && t.x <= r.maxX && t.y >= r.minY && t.y <= r.maxY) visit(key);
            }
        } else {
            for (int ty = r.minY; ty <= r.maxY; ++ty)
                for (int tx = r.minX; tx <= r.maxX; ++tx)
                    if (const TileKey key = TileUtils::packKey(tx, ty); lv.footprint.contains(key)) visit(key);
        }
    }
    if (changed) m_instancesDirty = true;
    m_computeDirty = !done;
    return changed;
}

bool RasterItem::rebuildInstances(const CanvasFrame& frame, const Range& range)
{
    for (Level& lv : m_levels) lv.atlas.beginInstances();
    const int L = m_drawLevel;
    Level& lv = m_levels[L];
    const auto place = [&](int level, int tx, int ty, float& x, float& y, float& size) {
        size = float(Tile::kSize << level);
        x = float(TileUtils::tileToWorld(tx)) * float(1 << level);
        y = float(TileUtils::tileToWorld(ty)) * float(1 << level);
    };
    const auto drawTile = [&](TileKey key) {
        const auto t = TileUtils::unpackKey(key);
        float x, y, size;
        place(L, t.x, t.y, x, y, size);
        if (lv.atlas.add(key, x, y, size)) return;   // current, or stale until recomputed
        if (lv.current.contains(key)) return;        // known to be empty

        // Not there yet: the matching part of the nearest coarser level that has it...
        for (int m = L + 1; m < kLevels; ++m) {
            const int up = m - L;
            const TileKey parent = TilePyramid::parentKey(key, up);
            if (!m_levels[m].atlas.contains(parent)) {
                if (m_levels[m].current.contains(parent)) return;   // empty up there too
                continue;
            }
            const float sub = 1.0f / float(1 << up);
            const int maskBits = (1 << up) - 1;
            m_levels[m].atlas.add(parent, x, y, size, float(t.x & maskBits) * sub, float(t.y & maskBits) * sub, sub);
            return;
        }
        // ...or the finer tiles under it.
        if (L > 0)
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i) {
                    float cx, cy, cs;
                    place(L - 1, 2 * t.x + i, 2 * t.y + j, cx, cy, cs);
                    m_levels[L - 1].atlas.add(TileUtils::packKey(2 * t.x + i, 2 * t.y + j), cx, cy, cs);
                }
    };
    const double rangeTiles = double(range.maxX - range.minX + 1) * double(range.maxY - range.minY + 1);
    if (double(lv.footprint.size()) <= rangeTiles) {
        for (TileKey key : lv.footprint) {
            const auto t = TileUtils::unpackKey(key);
            if (t.x >= range.minX && t.x <= range.maxX && t.y >= range.minY && t.y <= range.maxY) drawTile(key);
        }
    } else {
        for (int ty = range.minY; ty <= range.maxY; ++ty)
            for (int tx = range.minX; tx <= range.maxX; ++tx)
                if (const TileKey key = TileUtils::packKey(tx, ty); lv.footprint.contains(key)) drawTile(key);
    }
    bool changed = false;
    for (Level& l : m_levels) changed |= l.atlas.endInstances(frame.pipelines, m_opacity);
    return changed;
}

bool RasterItem::prepare(CanvasFrame& frame)
{
    catchUpSource();
    if (!m_source) {
        const bool had = chunkCount() > 0;
        m_visiblePending = false;
        return had && rebuildInstances(frame, {});
    }

    const int level = frame.lodLevel(kLevels);
    if (level != m_drawLevel) { m_drawLevel = level; m_instancesDirty = true; }
    const Range visible = rangeAt(frame, level, 0);
    const Range margin  = rangeAt(frame, level, 1);   // panning inside it reuses the instances
    if (margin != m_drawnRange || m_instancesDirty) m_computeDirty = true;   // moved, or another level
    bool changed = false;
    if (m_computeDirty) changed = computeVisible(frame, visible, margin);   // new pixels in existing slots draw differently too
    if (m_instancesDirty || margin != m_drawnRange) {
        changed |= rebuildInstances(frame, margin);
        m_drawnRange = margin;
        m_instancesDirty = false;
    }
    return changed && !m_suppressed;
}

void RasterItem::drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame)
{
    if (m_suppressed) return;
    frame.bind(pass, frame.pipelines.atlasPipeline);
    for (const Level& lv : m_levels)
        if (!lv.atlas.empty()) lv.atlas.draw(pass);
}

void RasterItem::releaseGpu()
{
    for (Level& lv : m_levels) {
        lv.atlas.release();
        lv.footprint.clear(); lv.current.clear(); lv.cpu.clear(); lv.cpuCurrent.clear();
    }
    m_fullSync = true;   // if it's used again, start from scratch
    m_instancesDirty = true;
}
