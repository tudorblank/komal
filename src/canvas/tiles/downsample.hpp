#pragma once
// Levels of detail, made one way: a tile shrunk by 2^levels (a box filter, premultiplied in and
// out) into its part of a coarser tile. RasterTiles folds every level straight from level 0.

#include "raster/tile.hpp"

// `src` (null: transparent) averaged in 2^levels-square blocks into part (px, py) of `dst` (a
// whole tile's pixels), whose side holds 2^levels parts.
void downsampleInto(RGBA* dst, const Tile* src, int levels, int px, int py);
