#pragma once
#include "canvas/item.hpp"
#include "canvas/tiles/atlas.hpp"
#include "node/node.hpp"

class CompositorNode;

// RasterItem (drawGpu): a compositor's output on the canvas, as a pyramid of levels of detail: level 0 is the
// output's own tiles, level L tiles are 64x64 pictures of 64 * 2^L world pixels, each the 2x2
// average of four level L-1 tiles. The view draws the level that matches its zoom (a texel
// is 0.5..1 screen pixel), so zoomed out it reads a few small tiles instead of every
// full-resolution one, and nothing aliases.
//
// Everything is lazy: a level's tiles are computed (downsampled on the CPU, kept there for the
// levels above) and uploaded only once they're on screen at that level, within the frame's
// budget. A tile the view needs but hasn't got yet is drawn from what is there meanwhile: the
// matching part of a coarser level, or the finer tiles under it, so zooming never shows holes.
//
// The source's dirty tiles invalidate the matching tile of every level; the old picture stays
// on screen until the new one replaces it. The source must have its cache enabled (that is
// what tracks dirty tiles), and an item should be its only reader of them.
class RasterItem : public CanvasItem {
public:
    explicit RasterItem(CompositorNode* source = nullptr) : m_source(source) {}
    ~RasterItem() override { releaseGpu(); }

    void setSource(CompositorNode* source);   // re-reads everything
    CompositorNode* source() const { return m_source; }
    void setOpacity(float opacity);
    // Suppressed: keeps catching up with its source but draws nothing (something else shows
    // the same picture meanwhile, see EditPreviewItem).
    void setSuppressed(bool suppressed);

    size_t chunkCount() const;   // tiles on the GPU, every level
    size_t pageCount()  const;
    int    drawLevel()  const { return m_drawLevel; }
    bool   syncPending() const { return m_visiblePending || m_fullSync; }

    bool prepare(CanvasFrame& frame) override;
    void drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override;
    bool settled() const override { return !m_visiblePending; }

private:
    static constexpr int kLevels = 10;   // level 9: one tile = 32768 world pixels

    struct Level {
        TileAtlas atlas;        // what the GPU has: current, or stale until recomputed
        TileSet   footprint;    // tiles that may hold something
        TileSet   current;      // the atlas is up to date here (absent from the atlas: empty)
        TileGrid  cpu;          // L >= 1: the downsampled pixels, for the level above
        TileSet   cpuCurrent;   // L >= 1: cpu is up to date here (absent from cpu: empty)
    };
    struct Range { int minX = 1, minY = 1, maxX = 0, maxY = 0; bool operator==(const Range&) const = default; };

    void catchUpSource();                     // full re-reads and dirty tiles -> invalidation
    void invalidate(TileKey key);             // a level-0 tile changed
    Range rangeAt(const CanvasFrame& frame, int level, int margin) const;
    bool computeVisible(CanvasFrame& frame, const Range& visible, const Range& margin);
    // The CPU picture of a tile of `level`; complete is false if the budget ran out first
    // (what was done is kept, the next call carries on).
    const Tile* lodTile(CanvasFrame& frame, int level, int tx, int ty, bool& complete);
    bool rebuildInstances(const CanvasFrame& frame, const Range& range);

    CompositorNode* m_source = nullptr;
    Level     m_levels[kLevels];
    bool      m_fullSync = true;          // re-read the whole source before anything else
    bool      m_visiblePending = false;   // some on-screen tiles of the draw level are missing or stale
    bool      m_instancesDirty = true;
    bool      m_computeDirty = true;      // tiles in range may be missing or stale
    int       m_drawLevel = 0;
    Range     m_drawnRange;
    RenderCtx m_render;                   // scratch for rendering tiles out of the graph
    float     m_opacity = 1.0f;
    bool      m_suppressed = false;
};
