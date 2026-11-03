#include "ui/panels/canvas/tiles/raster-tiles.hpp"

RasterTiles::RasterTiles(std::unique_ptr<TileSource> source, AtlasPages& pages, RenderCtx& render)
    : m_levels(std::move(source), render)
{
    for (TileAtlas& a : m_atlas) a.share(pages);
}

void RasterTiles::sync()
{
    if (m_levels.sync())
        for (TileAtlas& a : m_atlas) a.clear();   // started over
}

bool RasterTiles::build(const Deadline& budget, const CanvasPipelines& p, int level, TileKey key)
{
    // No room on the GPU: shown empty, and the cache says it's out of memory (TileCache::outOfMemory).
    return m_levels.build(budget, level, key, [&](int l, TileKey k, const Tile* pixels) {
        if (pixels) m_atlas[l].upload(p, k, pixels->getReadAll());
        else        m_atlas[l].free(k);
    });
}

size_t RasterTiles::tileCount() const
{
    size_t n = 0;
    for (const TileAtlas& a : m_atlas) n += a.tileCount();
    return n;
}

void RasterTiles::release()
{
    for (TileAtlas& a : m_atlas) a.clear();
    m_levels.clear();
}
