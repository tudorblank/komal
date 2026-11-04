#pragma once
// Raster tiles as compact binary, and back: one tile's content, or a set of tiles (each its tile
// coordinates and its content). The one way tiles become bytes: a .komal file's rasters use it,
// and history can keep tiles this way instead of as snapshots.
//
//   ByteWriter w;
//   encodeTiles(raster.tiles(), w);            // every tile, its (tx, ty) and its pixels
//   ByteReader r(w.bytes());
//   TileGrid back;
//   if (!decodeTiles(r, back)) ...             // damaged: nothing trusted, `back` to be thrown away
//
// A tile is written in the smallest of four forms (its first byte says which):
//   empty    no pixels (what a history delta writes for a tile that went)
//   solid    one colour everywhere: 4 bytes
//   packed   each row through PNG's predictive filters (none, sub, up, average, paeth: the one
//            that predicts it best), then zstd: gradients and photos become small numbers
//   raw      the pixels as they are, when packing wouldn't make them smaller
// Pixels are the tile's own: premultiplied RGBA8, row by row. Read back as untrusted data (made
// canonical), so a damaged file never puts a pixel in that the raster couldn't hold. A set is
// its count, then each tile in row order (ty, then tx): the same tiles always give the same bytes.

#include "base/bytes.hpp"
#include "raster/tile-grid.hpp"

// One tile's content: what's after its coordinates in a set.
void encodeTile(const Tile& tile, ByteWriter& out);
bool decodeTile(ByteReader& in, Tile& out);   // false: damaged (`out` holds whatever got through)

// Every tile of a grid, coordinates and content. A grid decoded is filled with what's read (on top
// of what it holds); false: damaged, a duplicate tile, or one out of the world.
void encodeTiles(const TileGrid& tiles, ByteWriter& out);
bool decodeTiles(ByteReader& in, TileGrid& out);
