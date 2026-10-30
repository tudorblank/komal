#pragma once
// A brush stroke being painted into one raster. While it lasts only its coverage grows (the brush
// stamps dabs into it): the raster and the graph are untouched, and the views show the stroke on
// the GPU from that coverage (canvas/preview). end() composes it over the raster's pixels: one
// raster commit.
//
//   if (stroke.begin(raster, params, at)) ...   // started (the first dab stamped, if any)
//   if (stroke.moveTo(path)) ...                // through every point: true if it stamped
//   const RasterCommitRef c = stroke.end(key);  // the commit, if the pixels changed

#include "document/history.hpp"
#include "node/node-types.hpp"
#include "raster/brush.hpp"

#include <memory>
#include <span>
#include <utility>
#include <vector>

class StrokeSession {
public:
    bool   active() const { return m_stroke.active(); }
    NodeId node() const { return m_raster ? m_raster->id() : kNoNodeId; }   // the raster it paints into

    // On `raster`, keeping its transparency if it's alpha-locked. False if one is going (end it first).
    bool begin(std::shared_ptr<RasterRootNode> raster, const BrushParams& params, StrokePoint at);
    bool moveTo(std::span<const StrokePoint> path);   // false: nothing new stamped
    RasterCommitRef end(GraphKey key);   // {} if no stroke, or the pixels didn't change

    // ---- while active, for the preview
    const TileGrid&    coverage() const { return m_stroke.scratch(); }   // white, premultiplied
    const BrushParams& params() const { return m_stroke.params(); }      // keepAlpha set from the raster
    BoundsI  bounds() const { return m_bounds; }                         // the pixels it adds so far: end()'s, exactly
    uint64_t revision() const { return m_revision; }                     // bumps whenever it stamps
    // The tiles stamped after `revision` (a reader's last seen). False when it can't tell (from
    // another stroke): read everything.
    bool changesSince(uint64_t revision, TileSet& out) const;

private:
    void take();   // what the brush stamped since: logged

    BrushStroke m_stroke;
    std::shared_ptr<RasterRootNode> m_raster;
    TileSet  m_touched;                                  // every tile the stroke stamped
    std::vector<std::pair<uint64_t, TileSet>> m_log;     // per stamp: its revision and tiles
    uint64_t m_revision = 0;                             // never reset: readers from an older stroke see "everything"
    uint64_t m_began = 0;                                // the revision this stroke began at
    BoundsI  m_bounds;
    std::unique_ptr<Tile> m_measure = std::make_unique<Tile>();   // take()'s scratch
};
