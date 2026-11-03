#pragma once
// Pixels from a TileSource as GPU tiles at every level of detail, built on demand and shared by
// every view that shows them: what one view built, the others reuse. The one way pixels reach
// the GPU (TileCache keeps them). Its TileLevels makes the levels on the CPU (node/tile-levels);
// this uploads each tile into its level's atlas as it's made.
//
// sync() once a frame, before any view builds: the source's changed tiles make every level's
// tiles over them stale (their old pixels stay in the atlas until rebuilt); a full change starts
// over.

#include "ui/panels/canvas/tiles/atlas.hpp"
#include "node/tile-levels.hpp"

#include <memory>

class RasterTiles {
public:
    static constexpr int kLevels = TileLevels::kLevels;

    // `pages` and `render` (the cache's, shared by every RasterTiles) outlive it.
    RasterTiles(std::unique_ptr<TileSource> source, AtlasPages& pages, RenderCtx& render);
    ~RasterTiles() { release(); }
    RasterTiles(const RasterTiles&) = delete;
    RasterTiles& operator=(const RasterTiles&) = delete;

    void sync();
    // Bumps whenever anything changed: views compare it with what their draw was built from.
    uint64_t revision() const { return m_levels.revision(); }
    // Bumps when pixels in a level's atlas changed: a view drawing from it shows them only by rendering again.
    uint64_t revision(int level) const { return m_levels.revision(level); }

    const TileSet& footprint(int level) { return m_levels.footprint(level); }   // tiles that may hold something
    // Up to date in the atlas (absent from it: empty).
    bool current(int level, TileKey key) const { return m_levels.current(level, key); }
    // Builds and uploads tile `key` of `level`, unless current. False when the budget ran out first.
    bool build(const Deadline& budget, const CanvasPipelines& p, int level, TileKey key);
    const TileAtlas& atlas(int level) const { return m_atlas[level]; }

    size_t tileCount() const;   // on the GPU, every level
    void release();             // GPU tiles and built levels; the next sync() starts over

private:
    TileLevels m_levels;
    TileAtlas  m_atlas[kLevels];   // per level: current, or stale until rebuilt
};
