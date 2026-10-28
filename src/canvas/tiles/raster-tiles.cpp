#include "canvas/tiles/raster-tiles.hpp"

#include <utility>

RasterTiles::RasterTiles()
{
    for (Level& lv : m_levels) lv.atlas.share(m_pages);
}

void RasterTiles::setSource(Node* source)
{
    if (source == m_source) return;
    m_source = source;
    m_watch  = source ? source->watchOutput() : nullptr;
    m_fullSync = true;
}

void RasterTiles::sync()
{
    m_pages.recycle();   // what views drew last frame is no longer read
    if (!m_source) {
        if (std::exchange(m_fullSync, false)) {
            m_pyramid.clear();
            resetLevels();
        }
        return;
    }
    TileSet changed;
    if (m_watch->take(changed) || m_fullSync) {   // start over from what the output can cover
        m_fullSync = false;
        Node* src = m_source;
        m_pyramid.reset([this, src](int tx, int ty) { return renderTile(*src, tx, ty, m_render); }, src->footprint());
        resetLevels();
        return;
    }
    for (TileKey key : changed) invalidate(key);
}

void RasterTiles::resetLevels()
{
    for (Level& lv : m_levels) {
        lv.atlas.clear();
        lv.current.clear();
    }
    touchAll();
}

void RasterTiles::touchAll()
{
    for (uint64_t& r : m_levelRevision) ++r;
    ++m_revision;
}

void RasterTiles::invalidate(TileKey key)
{
    m_pyramid.invalidate(key);
    for (int l = 0; l < kLevels; ++l) m_levels[l].current.erase(TilePyramid::parentKey(key, l));   // the atlas keeps the old picture
    ++m_revision;
}

bool RasterTiles::build(CanvasFrame& frame, int level, TileKey key)
{
    Level& lv = m_levels[level];
    if (lv.current.contains(key)) return true;
    if (!frame.inBudget()) return false;
    const auto t = TileUtils::unpackKey(key);
    bool complete = true;
    const Tile* tile = m_pyramid.tile(frame, level, t.x, t.y, complete);
    if (!complete) return false;
    if (tile) lv.atlas.upload(frame.pipelines, key, tile->getReadAll());
    else      lv.atlas.free(key);
    lv.current.insert(key);
    ++m_levelRevision[level];
    ++m_revision;
    return true;
}

size_t RasterTiles::tileCount() const
{
    size_t n = 0;
    for (const Level& lv : m_levels) n += lv.atlas.tileCount();
    return n;
}

size_t RasterTiles::pageCount() const
{
    return m_pages.pageCount();
}

void RasterTiles::release()
{
    for (Level& lv : m_levels) {
        lv.atlas.clear();
        lv.current.clear();
    }
    m_pages.release();
    m_pyramid.clear();
    m_fullSync = true;   // used again: start from scratch
    touchAll();
}
