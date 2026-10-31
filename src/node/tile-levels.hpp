#pragma once
// A TileSource's pixels at every level of detail, on the CPU (a level L pixel is the average of
// 2^L x 2^L level 0 pixels): which tiles each level has, which are current, and the folds that
// make them. Built on demand and kept current as the source changes: what RasterTiles
// (canvas/tiles) uploads to the GPU, told tile by tile as each becomes current.
//
// A level L tile is folded straight from the level 0 tiles under it (downsampleInto), so only
// the levels asked for are kept; past level 6 a texel spans more than a level 0 tile, so those
// fold level L - 6 tiles instead. A build that runs out of budget resumes where it stopped.
//
//   if (levels.sync()) ...                         // once a frame: true when it started over
//   levels.build(budget, level, key, [&](int l, TileKey k, const Tile* pixels) { ... });

#include "base/deadline.hpp"
#include "node/tile-source.hpp"
#include "raster/downsample.hpp"

#include <absl/container/flat_hash_map.h>
#include <functional>
#include <memory>
#include <vector>

class TileLevels {
public:
    static constexpr int kLevels = kLodLevels;
    // A tile just made current: its pixels (null: empty there), only for the call.
    using Built = std::function<void(int level, TileKey key, const Tile* pixels)>;

    // `render` (the caller's, shared) outlives it.
    TileLevels(std::unique_ptr<TileSource> source, RenderCtx& render) : m_source(std::move(source)), m_render(&render) {}

    // Follows the source: its changed tiles make every level's tiles over them stale (a rebuild
    // folds only what changed). True when it started over: nothing built is current.
    bool sync();
    uint64_t revision() const { return m_revision; }                       // bumps whenever anything changed
    uint64_t revision(int level) const { return m_levels[level].revision; }   // bumps when a level's tiles change

    const TileSet& footprint(int level);   // tiles that may hold something
    bool current(int level, TileKey key) const { return m_levels[level].current.contains(key); }
    // Makes tile `key` of `level` current, unless it is: `built` hears it, and every tile of a
    // finer level made on the way. False when the budget ran out first.
    bool build(const Deadline& budget, int level, TileKey key, const Built& built);
    void clear();   // everything it made; the next sync() starts over

    static TileKey parentKey(TileKey key, int levels);

private:
    static constexpr int kMaxFold = 6;   // levels one level 0 tile folds down (a texel per tile at most)

    struct Level {
        TileSet   current;
        TileSet   built;      // built at least once (current or stale)
        TileGrid  cpu;        // levels >= 1: what was made, to fold changes into
        absl::flat_hash_map<TileKey, TileSet> pending;    // being built: the base level tiles still to fold
        absl::flat_hash_map<TileKey, std::vector<TileKey>> children;   // the base level tiles under each tile
        TileSet   footprint;
        bool      footprintReady = false;
        uint64_t  revision = 1;
    };

    static int baseOf(int level) { return level > kMaxFold ? level - kMaxFold : 0; }   // what a level folds
    void restart();
    void markStale(TileKey key);   // level 0 tile `key` changed
    bool fold(const Deadline& budget, int level, TileKey key, const Built& built);

    std::unique_ptr<TileSource> m_source;
    RenderCtx*  m_render;
    Level       m_levels[kLevels];
    bool        m_fullSync = true;   // read the whole source first
    uint64_t    m_revision = 1;
};
