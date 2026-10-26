#include "raster.hpp"
#include "core/perf.hpp"
#include "node/node-types.hpp"

#include <algorithm>
#include <utility>

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

void RasterItem::resetLevels()
{
    for (Level& lv : m_levels) {
        lv.atlas.clear();
        lv.current.clear();
    }
    m_instancesDirty = true;
    m_computeDirty = true;
}

void RasterItem::catchUpSource()
{
    if (!m_source) {
        if (std::exchange(m_fullSync, false)) {
            m_pyramid.clear();
            resetLevels();
        }
        return;
    }
    if (m_source->takeFullSync() || m_fullSync) {   // start over from what the output can cover
        m_fullSync = false;
        CompositorNode* src = m_source;
        m_pyramid.reset([this, src](int tx, int ty) { return renderTile(*src, tx, ty, m_render); }, src->footprint());
        resetLevels();
        src->drainDirtyTiles();   // covered by starting over
        return;
    }
    for (TileKey key : m_source->drainDirtyTiles()) invalidate(key);
}

void RasterItem::invalidate(TileKey key)
{
    m_pyramid.invalidate(key);
    for (int l = 0; l < kLevels; ++l) m_levels[l].current.erase(TilePyramid::parentKey(key, l));   // the atlas keeps the old picture
    m_computeDirty = true;
    m_instancesDirty = true;
}

TileRect RasterItem::rangeAt(const CanvasFrame& frame, int level, int margin) const
{
    using TileUtils::worldToTileClamped;
    return { (worldToTileClamped(frame.view.minX) >> level) - margin, (worldToTileClamped(frame.view.minY) >> level) - margin,
             (worldToTileClamped(frame.view.maxX) >> level) + margin, (worldToTileClamped(frame.view.maxY) >> level) + margin };
}

bool RasterItem::computeVisible(CanvasFrame& frame, const TileRect& visible, const TileRect& margin)
{
    PerfScope perf(Perf::CanvasSync);
    Level& lv = m_levels[m_drawLevel];
    const TileSet& footprint = m_pyramid.footprint(m_drawLevel);
    bool changed = false;
    bool done = true;   // every tile of both ranges is current
    m_visiblePending = false;

    // Visible tiles first, then the margin (ready before a pan shows them).
    for (int pass = 0; pass < 2; ++pass) {
        forEachTileIn(footprint, pass == 0 ? visible : margin, [&](TileKey key) {
            if (lv.current.contains(key)) return;
            const auto t = TileUtils::unpackKey(key);
            if (pass == 1 && visible.contains(t.x, t.y)) return;
            bool complete = frame.inBudget();
            const Tile* tile = complete ? m_pyramid.tile(frame, m_drawLevel, t.x, t.y, complete) : nullptr;
            if (!complete) {
                if (pass == 0) m_visiblePending = true;
                done = false;
                return;
            }
            if (tile) lv.atlas.upload(frame.pipelines, key, tile->getReadAll());
            else      lv.atlas.free(key);
            lv.current.insert(key);
            changed = true;
        });
    }
    if (changed) m_instancesDirty = true;
    m_computeDirty = !done;
    return changed;
}

bool RasterItem::rebuildInstances(const CanvasFrame& frame, const TileRect& range)
{
    for (Level& lv : m_levels) lv.atlas.beginInstances();
    const int L = m_drawLevel;
    Level& lv = m_levels[L];
    const auto worldOf = [](int level, int t) { return float(TileUtils::tileToWorld(t)) * float(1 << level); };

    forEachTileIn(m_pyramid.footprint(L), range, [&](TileKey key) {
        const auto t = TileUtils::unpackKey(key);
        const float x = worldOf(L, t.x), y = worldOf(L, t.y), size = float(Tile::kSize << L);
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
            const int bits = (1 << up) - 1;
            m_levels[m].atlas.add(parent, x, y, size, float(t.x & bits) * sub, float(t.y & bits) * sub, sub);
            return;
        }
        // ...or the finer tiles under it.
        if (L > 0)
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i)
                    m_levels[L - 1].atlas.add(TileUtils::packKey(2 * t.x + i, 2 * t.y + j), worldOf(L - 1, 2 * t.x + i),
                                              worldOf(L - 1, 2 * t.y + j), float(Tile::kSize << (L - 1)));
    });
    bool changed = false;
    for (Level& l : m_levels) changed |= l.atlas.endInstances(frame.pipelines, m_opacity);
    return changed;
}

bool RasterItem::prepare(CanvasFrame& frame)
{
    catchUpSource();
    if (!m_source) {
        m_visiblePending = false;
        const bool changed = m_instancesDirty && rebuildInstances(frame, {});   // draws nothing from now on
        m_instancesDirty = false;
        return changed && !m_suppressed;
    }

    const int level = frame.lodLevel(kLevels);
    if (level != m_drawLevel) { m_drawLevel = level; m_instancesDirty = true; }
    const TileRect visible = rangeAt(frame, level, 0);
    const TileRect margin  = rangeAt(frame, level, 1);   // panning inside it reuses the instances
    if (margin != m_drawnRange || m_instancesDirty) m_computeDirty = true;
    bool changed = false;
    if (m_computeDirty) changed = computeVisible(frame, visible, margin);   // new pixels in old slots count too
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
        lv.current.clear();
    }
    m_pyramid.clear();
    m_fullSync = true;   // used again: start from scratch
    m_instancesDirty = true;
}
