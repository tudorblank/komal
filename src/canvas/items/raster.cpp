#include "raster.hpp"
#include "core/perf.hpp"

#include <algorithm>

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

TileRect RasterItem::rangeAt(const CanvasFrame& frame, int level, int margin) const
{
    using TileUtils::worldToTileClamped;
    return { (worldToTileClamped(frame.view.minX) >> level) - margin, (worldToTileClamped(frame.view.minY) >> level) - margin,
             (worldToTileClamped(frame.view.maxX) >> level) + margin, (worldToTileClamped(frame.view.maxY) >> level) + margin };
}

bool RasterItem::computeVisible(CanvasFrame& frame, const TileRect& visible, const TileRect& margin)
{
    PerfScope perf(Perf::CanvasSync);
    const int L = m_drawLevel;
    const TileSet& footprint = m_tiles->footprint(L);
    bool changed = false;
    bool done = true;   // every tile of both ranges is current
    m_visiblePending = false;

    // Visible tiles first, then the margin (ready before a pan shows them).
    for (int pass = 0; pass < 2; ++pass) {
        forEachTileIn(footprint, pass == 0 ? visible : margin, [&](TileKey key) {
            if (m_tiles->current(L, key)) return;
            const auto t = TileUtils::unpackKey(key);
            if (pass == 1 && visible.contains(t.x, t.y)) return;
            if (!m_tiles->build(frame, L, key)) {
                if (pass == 0) m_visiblePending = true;
                done = false;
                return;
            }
            changed = true;
        });
    }
    if (changed) m_instancesDirty = true;
    m_computeDirty = !done;
    return changed;
}

bool RasterItem::rebuildInstances(const CanvasFrame& frame, const TileRect& range)
{
    for (int l = 0; l < kLevels; ++l) m_draw[l].begin(m_tiles->atlas(l));
    const int L = m_drawLevel;
    const auto worldOf = [](int level, int t) { return float(TileUtils::tileToWorld(t)) * float(1 << level); };

    forEachTileIn(m_tiles->footprint(L), range, [&](TileKey key) {
        const auto t = TileUtils::unpackKey(key);
        const float x = worldOf(L, t.x), y = worldOf(L, t.y), size = float(Tile::kSize << L);
        if (m_draw[L].add(key, x, y, size)) return;   // current, or stale until recomputed
        if (m_tiles->current(L, key)) return;         // known to be empty

        // Not there yet: the matching part of the nearest coarser level that has it...
        for (int m = L + 1; m < kLevels; ++m) {
            const int up = m - L;
            const TileKey parent = TilePyramid::parentKey(key, up);
            if (!m_tiles->atlas(m).contains(parent)) {
                if (m_tiles->current(m, parent)) return;   // empty up there too
                continue;
            }
            const float sub = 1.0f / float(1 << up);
            const int bits = (1 << up) - 1;
            m_draw[m].add(parent, x, y, size, float(t.x & bits) * sub, float(t.y & bits) * sub, sub);
            return;
        }
        // ...or the finer tiles under it.
        if (L > 0)
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i)
                    m_draw[L - 1].add(TileUtils::packKey(2 * t.x + i, 2 * t.y + j), worldOf(L - 1, 2 * t.x + i),
                                      worldOf(L - 1, 2 * t.y + j), float(Tile::kSize << (L - 1)));
    });
    bool changed = false;
    for (TileDrawList& d : m_draw) changed |= d.end(frame.pipelines, m_opacity);
    return changed;
}

bool RasterItem::prepare(CanvasFrame& frame)
{
    if (m_tiles->revision() != m_seen) {   // invalidated, or built by another view
        m_computeDirty   = true;
        m_instancesDirty = true;
    }
    bool changed = false;
    for (int l = 0; l < kLevels; ++l)   // new pixels in slots this view draws from (another view uploaded them)
        if (m_tiles->revision(l) != m_seenLevel[l] && !m_draw[l].empty()) changed = true;
    if (!m_tiles->source()) {
        m_visiblePending = false;
        if (m_instancesDirty) changed = rebuildInstances(frame, {});   // draws nothing from now on
        m_instancesDirty = false;
    } else {
        const int level = frame.lodLevel(kLevels);
        if (level != m_drawLevel) { m_drawLevel = level; m_instancesDirty = true; }
        const TileRect visible = rangeAt(frame, level, 0);
        const TileRect margin  = rangeAt(frame, level, 1);   // panning inside it reuses the instances
        if (margin != m_drawnRange || m_instancesDirty) m_computeDirty = true;
        if (m_computeDirty) changed = computeVisible(frame, visible, margin);   // new pixels in old slots count too
        if (m_instancesDirty || margin != m_drawnRange) {
            changed |= rebuildInstances(frame, margin);
            m_drawnRange = margin;
            m_instancesDirty = false;
        }
    }
    m_seen = m_tiles->revision();
    for (int l = 0; l < kLevels; ++l) m_seenLevel[l] = m_tiles->revision(l);
    return changed && !m_suppressed;
}

void RasterItem::drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame)
{
    if (m_suppressed) return;
    frame.bind(pass, frame.pipelines.atlasPipeline);
    for (const TileDrawList& d : m_draw) d.draw(pass);
}

void RasterItem::releaseGpu()
{
    for (TileDrawList& d : m_draw) d.release();
    m_instancesDirty = true;
}
