#pragma once
// TilePyramid: levels of detail of a tile source, on the CPU. Level 0 is the source itself; a
// tile of level L is its four children of level L-1 halved (2x2 averages), covering 64 << L
// world pixels. Tiles of levels >= 1 are built on demand, within the frame's budget, and kept:
// running out of budget half-way loses nothing.
//
// Used for the edit preview's frozen inputs (items/preview); RasterItem keeps its own levels
// (it also invalidates them tile by tile) but shares the downsampling.

#include "canvas/item.hpp"
#include "raster/tile-grid.hpp"

#include <functional>

class TilePyramid {
public:
    static constexpr int kLevels = 10;   // level 9: one tile = 32768 world pixels

    // Level 0: tile (tx, ty), or nullptr if empty. The tile only needs to stay valid until the
    // next call.
    using Source = std::function<const Tile*(int tx, int ty)>;

    void reset(Source source, TileSet footprint);
    const TileSet& footprint(int level);   // tiles of `level` that may hold something

    // Tile (tx, ty) of `level`, or nullptr if it's empty. `complete` turns false when the frame
    // ran out of budget first: ask again next frame.
    const Tile* tile(const CanvasFrame& frame, int level, int tx, int ty, bool& complete);

    static TileKey parentKey(TileKey key, int levels);
    // One quadrant (qx, qy) of `dst` = `src` halved. Premultiplied in, out.
    static void downsampleQuadrant(RGBA* dst, const Tile& src, int qx, int qy);

private:
    struct Level {
        TileSet  footprint;
        bool     footprintReady = false;
        TileGrid cpu;       // L >= 1: built tiles
        TileSet  current;   // L >= 1: built (absent from cpu: empty)
    };
    Source m_source;
    Level  m_levels[kLevels];
};
