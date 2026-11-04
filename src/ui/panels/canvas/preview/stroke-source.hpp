#pragma once
// The stroke being painted, as a TileSource: its coverage so far (Workspace::stroke()), the tiles
// stamped since it last asked. A preview's leaf: the stroke over its raster, on the GPU.

#include "node/tile-source.hpp"

class Workspace;

class StrokeSource : public TileSource {
public:
    explicit StrokeSource(const Workspace& workspace) : m_workspace(workspace) {}

    const Tile* tile(int tx, int ty, RenderCtx& ctx) override;
    TileSet footprint() override;
    bool takeChanges(TileSet& changed) override;

private:
    const Workspace& m_workspace;
    uint64_t m_seen = 0;   // the stroke's revision, as last read
};
