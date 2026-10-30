#pragma once
// Pixels from a TileSource as GPU tiles at every level of detail (a level L texel is the average
// of 2^L x 2^L level 0 pixels), built on demand and shared by every view that shows them: what
// one view built, the others reuse. The one way pixels reach the GPU (TileCache keeps them).
//
// A level L tile is folded straight from the level 0 tiles under it (downsampleInto), so only
// the levels asked for are kept (on the CPU too, to fold changes into); past level 6 a texel
// spans more than a level 0 tile, so those fold level L - 6 tiles instead. A build that runs out
// of budget resumes where it stopped.
//
// sync() once a frame, before any view builds: the source's changed tiles make every level's
// tiles over them stale (their old pixels stay in the atlas until rebuilt; a rebuild folds only
// what changed); a full change starts over.

#include "base/deadline.hpp"
#include "canvas/fit.hpp"
#include "canvas/tiles/atlas.hpp"
#include "canvas/tiles/tile-source.hpp"

#include <absl/container/flat_hash_map.h>
#include <memory>
#include <vector>

class RasterTiles {
public:
    static constexpr int kLevels = kLodLevels;

    // `pages` and `render` (the cache's, shared by every RasterTiles) outlive it.
    RasterTiles(std::unique_ptr<TileSource> source, AtlasPages& pages, RenderCtx& render);
    ~RasterTiles() { release(); }
    RasterTiles(const RasterTiles&) = delete;
    RasterTiles& operator=(const RasterTiles&) = delete;

    void sync();
    // Bumps whenever anything changed: views compare it with what their draw was built from.
    uint64_t revision() const { return m_revision; }
    // Bumps when pixels in a level's atlas changed: a view drawing from it shows them only by rendering again.
    uint64_t revision(int level) const { return m_levels[level].revision; }

    const TileSet& footprint(int level);   // tiles that may hold something
    // Up to date in the atlas (absent from it: empty).
    bool current(int level, TileKey key) const { return m_levels[level].current.contains(key); }
    // Builds and uploads tile `key` of `level`, unless current. False when the budget ran out first.
    bool build(const Deadline& budget, const CanvasPipelines& p, int level, TileKey key);
    const TileAtlas& atlas(int level) const { return m_levels[level].atlas; }

    static TileKey parentKey(TileKey key, int levels);
    size_t tileCount() const;   // on the GPU, every level
    void release();             // GPU tiles and built levels; the next sync() starts over

private:
    static constexpr int kMaxFold = 6;   // levels one level 0 tile folds down (a texel per tile at most)

    struct Level {
        TileAtlas atlas;      // current, or stale until rebuilt
        TileSet   current;
        TileSet   built;      // built at least once (current or stale)
        TileGrid  cpu;        // levels >= 1: what the atlas holds, to fold changes into
        absl::flat_hash_map<TileKey, TileSet> pending;    // being built: the base level tiles still to fold
        absl::flat_hash_map<TileKey, std::vector<TileKey>> children;   // the base level tiles under each tile
        TileSet   footprint;
        bool      footprintReady = false;
        uint64_t  revision = 1;
    };

    static int baseOf(int level) { return level > kMaxFold ? level - kMaxFold : 0; }   // what a level folds
    void restart();
    void markStale(TileKey key);   // level 0 tile `key` changed
    bool fold(const Deadline& budget, const CanvasPipelines& p, int level, TileKey key);

    std::unique_ptr<TileSource> m_source;
    RenderCtx*  m_render;
    Level       m_levels[kLevels];
    bool        m_fullSync = true;   // read the whole source first
    uint64_t    m_revision = 1;
};
