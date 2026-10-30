#pragma once
// The stroke being painted, as a TileSource: its coverage so far (Project::stroke()), the tiles
// stamped since it last asked. A preview's leaf: the stroke over its raster, on the GPU.

#include "canvas/tiles/tile-source.hpp"

class Project;

class StrokeSource : public TileSource {
public:
    explicit StrokeSource(const Project& project) : m_project(project) {}

    const Tile* tile(int tx, int ty, RenderCtx& ctx) override;
    TileSet footprint() override;
    bool takeChanges(TileSet& changed) override;

private:
    const Project& m_project;
    uint64_t m_seen = 0;   // the stroke's revision, as last read
};
