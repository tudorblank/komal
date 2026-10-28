#pragma once
#include "canvas/item.hpp"
#include "canvas/tiles/raster-tiles.hpp"

// A compositor's output in one view, drawn at the level of detail that matches the zoom (a texel
// covers 0.5..1 screen pixels), so zoomed out it reads a few coarse tiles. The tiles are shared
// (RasterTiles): this item builds the ones it shows and keeps its own draw lists.
//
// Lazy: a level's tiles are built only once they're on screen, within the frame budget. A
// missing tile is drawn from a coarser level, or the finer tiles under it, so zooming never
// shows holes; the old picture stays until the new one replaces it.
class RasterItem : public CanvasItem {
public:
    explicit RasterItem(RasterTiles& tiles) : m_tiles(&tiles) {}
    ~RasterItem() override { releaseGpu(); }

    void setOpacity(float opacity);
    // Keeps catching up with the source but draws nothing (EditPreviewItem shows it meanwhile).
    void setSuppressed(bool suppressed);

    int  drawLevel()   const { return m_drawLevel; }
    bool syncPending() const { return m_visiblePending; }

    bool prepare(CanvasFrame& frame) override;
    void drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override;
    // Also false when the tiles changed since this view's draw was built (another view built some).
    bool settled() const override { return !m_visiblePending && m_seen == m_tiles->revision(); }

private:
    static constexpr int kLevels = RasterTiles::kLevels;

    TileRect rangeAt(const CanvasFrame& frame, int level, int margin) const;
    bool computeVisible(CanvasFrame& frame, const TileRect& visible, const TileRect& margin);
    bool rebuildInstances(const CanvasFrame& frame, const TileRect& range);

    RasterTiles* m_tiles;
    TileDrawList m_draw[kLevels];         // per level, what this view draws from its atlas
    uint64_t     m_seen = 0;              // the tiles' revision the draw was built from
    uint64_t     m_seenLevel[kLevels] = {};   // each level's, as last rendered
    bool         m_visiblePending = false;   // on-screen tiles of the draw level missing or stale
    bool         m_instancesDirty = true;
    bool         m_computeDirty = true;      // tiles in range may be missing or stale
    int          m_drawLevel = 0;
    TileRect     m_drawnRange;
    float        m_opacity = 1.0f;
    bool         m_suppressed = false;
};
