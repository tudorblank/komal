#include "document/stroke-session.hpp"
#include "base/perf.hpp"

bool StrokeSession::begin(std::shared_ptr<RasterRootNode> raster, const BrushParams& params, StrokePoint at)
{
    if (active() || !raster) return false;
    PerfScope perf(Perf::Stroke);
    BrushParams p = params;
    p.keepAlpha = raster->alphaLocked();
    m_raster = std::move(raster);
    m_touched.clear();
    m_log.clear();
    m_bounds = {};
    m_began = ++m_revision;
    m_stroke.begin(p, at);
    take();
    return true;
}

bool StrokeSession::moveTo(std::span<const StrokePoint> path)
{
    if (!active() || path.empty()) return false;
    PerfScope perf(Perf::Stroke);
    const uint64_t before = m_revision;
    for (const StrokePoint& p : path) m_stroke.moveTo(p);
    take();
    return m_revision != before;
}

void StrokeSession::take()
{
    TileSet tiles = m_stroke.takeDirty();
    if (tiles.empty()) return;
    for (TileKey key : tiles) {
        const auto [tx, ty] = TileUtils::unpackKey(key);
        m_touched.insert(key);
        // The pixels end() will add, by the brush's own rule: the stroke over nothing. A tile
        // already inside the bounds can't grow them.
        const int x0 = TileUtils::tileToWorld(tx), y0 = TileUtils::tileToWorld(ty);
        const BoundsI whole{ x0, y0, x0 + Tile::kSize - 1, y0 + Tile::kSize - 1 };
        if (m_bounds.intersect(whole) == whole || !m_stroke.composeTile(*m_measure, nullptr, tx, ty)) continue;
        const BoundsI& lb = m_measure->bounds();
        m_bounds.expand(BoundsI{ x0 + lb.minX, y0 + lb.minY, x0 + lb.maxX, y0 + lb.maxY });
    }
    m_log.push_back({ ++m_revision, std::move(tiles) });
}

bool StrokeSession::changesSince(uint64_t revision, TileSet& out) const
{
    if (revision < m_began) return false;
    for (const auto& [rev, tiles] : m_log)
        if (rev > revision) out.insert(tiles.begin(), tiles.end());
    return true;
}

RasterCommitRef StrokeSession::end(GraphKey key)
{
    if (!active()) return {};
    PerfScope perf(Perf::Stroke);
    const std::shared_ptr<RasterRootNode> raster = std::exchange(m_raster, nullptr);
    RasterCommitId commit = kNoRasterCommit;
    if (!m_touched.empty())   // the whole stroke over the raster's pixels, once
        commit = raster->recordEdit(key, [&](RasterEditor& ed) { applyStroke(ed, raster->raster(), m_stroke, m_touched); });
    m_stroke.end();
    m_log.clear();
    if (commit == kNoRasterCommit) return {};
    return { raster->raster().id(), commit };
}
