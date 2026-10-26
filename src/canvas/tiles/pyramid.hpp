#pragma once
// Levels of detail of a tile source, on the CPU. Level 0 is the source; a level L tile is its
// four level L-1 children averaged 2x2, covering 64 << L world pixels. Levels >= 1 are built on
// demand within the frame budget and kept, so running out of budget loses nothing.

#include "canvas/item.hpp"
#include "raster/tile-grid.hpp"

#include <functional>

class TilePyramid {
public:
    static constexpr int kLevels = 10;   // level 9: one tile = 32768 world pixels

    // Level 0 tile (tx, ty), or nullptr if empty. Must stay valid until the next call.
    using Source = std::function<const Tile*(int tx, int ty)>;

    void reset(Source source, TileSet footprint);   // footprint: the level 0 tiles that may hold something
    void clear() { reset({}, {}); }
    void invalidate(TileKey key);                   // level 0 tile `key` changed (or appeared)

    const TileSet& footprint(int level);            // tiles of `level` that may hold something
    bool isCurrent(int level, TileKey key) const { return level == 0 || m_levels[level].current.contains(key); }

    // Tile (tx, ty) of `level`, or nullptr if empty. `complete` turns false when the budget ran
    // out first: ask again next frame.
    const Tile* tile(const CanvasFrame& frame, int level, int tx, int ty, bool& complete);

    static TileKey parentKey(TileKey key, int levels);
    // Quadrant (qx, qy) of `dst` = `src` halved. Premultiplied in and out.
    static void downsampleQuadrant(RGBA* dst, const Tile& src, int qx, int qy);

private:
    struct Level {
        TileSet  footprint;
        bool     footprintReady = false;
        TileGrid cpu;       // L >= 1: built tiles
        TileSet  current;   // L >= 1: built and up to date (absent from cpu: empty)
    };
    Source m_source;
    Level  m_levels[kLevels];
};
