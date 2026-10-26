#pragma once
#include "canvas/item.hpp"
#include "canvas/tiles/atlas.hpp"
#include "canvas/tiles/pyramid.hpp"
#include "node/node.hpp"

class CompositorNode;

// A compositor's output on the canvas, drawn at the level of detail that matches the zoom
// (a texel covers 0.5..1 screen pixels), so zoomed out it reads a few coarse tiles.
//
// Lazy: a level's tiles are built (TilePyramid) and uploaded only once they're on screen, within
// the frame budget. A missing tile is drawn from a coarser level, or the finer tiles under it,
// so zooming never shows holes. Dirty source tiles invalidate every level; the old picture stays
// until the new one replaces it. The source must have its cache enabled (it tracks dirty tiles),
// and this item should be its only reader of them.
class RasterItem : public CanvasItem {
public:
    explicit RasterItem(CompositorNode* source = nullptr) : m_source(source) {}
    ~RasterItem() override { releaseGpu(); }

    void setSource(CompositorNode* source);   // re-reads everything
    CompositorNode* source() const { return m_source; }
    void setOpacity(float opacity);
    // Keeps catching up with the source but draws nothing (EditPreviewItem shows it meanwhile).
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
    static constexpr int kLevels = TilePyramid::kLevels;

    struct Level {
        TileAtlas atlas;     // current, or stale until rebuilt
        TileSet   current;   // the atlas is up to date here (absent from it: empty)
    };

    void catchUpSource();   // full re-reads and dirty tiles -> invalidation
    void resetLevels();
    void invalidate(TileKey key);
    TileRect rangeAt(const CanvasFrame& frame, int level, int margin) const;
    bool computeVisible(CanvasFrame& frame, const TileRect& visible, const TileRect& margin);
    bool rebuildInstances(const CanvasFrame& frame, const TileRect& range);

    CompositorNode* m_source = nullptr;
    TilePyramid m_pyramid;
    Level       m_levels[kLevels];
    bool        m_fullSync = true;          // re-read the whole source first
    bool        m_visiblePending = false;   // on-screen tiles of the draw level missing or stale
    bool        m_instancesDirty = true;
    bool        m_computeDirty = true;      // tiles in range may be missing or stale
    int         m_drawLevel = 0;
    TileRect    m_drawnRange;
    RenderCtx   m_render;
    float       m_opacity = 1.0f;
    bool        m_suppressed = false;
};
