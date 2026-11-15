#include "ui/panels/canvas/tiles/raster-tiles.hpp"

#include <utility>

RasterTiles::RasterTiles(std::unique_ptr<TileSource> source, AtlasPages& pages, RenderCtx& render)
    : m_levels(std::move(source), render),
      m_atlas([&]<size_t... I>(std::index_sequence<I...>) { return std::array<TileAtlas, kLevels>{ ((void)I, TileAtlas(pages))... }; }(
          std::make_index_sequence<kLevels>{}))
{
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
        if (pixels) m_atlas[size_t(l)].upload(p, k, pixels->getReadAll());
        else        m_atlas[size_t(l)].free(k);
    });
}

void RasterTiles::drawLevel(TileDrawList& draw, int level, const CanvasPipelines& p)
{
    const float size = float(Tile::kSize << level);
    draw.begin(atlas(level));
    for (TileKey key : footprint(level)) {
        const auto t = TileUtils::unpackKey(key);
        draw.add(key, float(t.x) * size, float(t.y) * size, size);
    }
    draw.end(p);
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
