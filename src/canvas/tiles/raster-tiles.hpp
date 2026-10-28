#pragma once
// A node's output as GPU tiles at every level of detail (level L halves L-1, TilePyramid), shared
// by every view that shows it: the canvas, the navigator. A view builds the tiles it draws,
// within its frame's budget, and keeps its own draw lists (RasterItem); what one view built, the
// others reuse. Every level's tiles share one set of pages.
//
// sync() once a frame, before any view: the output tiles the source reports changed (its
// TileWatch) invalidate every level (the old picture stays in the atlas until rebuilt); a full
// re-read starts over. A source that caches its output (the master) is cheap to re-read.

#include "canvas/tiles/atlas.hpp"
#include "canvas/tiles/pyramid.hpp"
#include "node/node.hpp"

class RasterTiles {
public:
    static constexpr int kLevels = TilePyramid::kLevels;

    RasterTiles();
    ~RasterTiles() { release(); }
    RasterTiles(const RasterTiles&) = delete;
    RasterTiles& operator=(const RasterTiles&) = delete;

    void setSource(Node* source);   // re-reads everything
    Node* source() const { return m_source; }

    void sync();
    // Bumps whenever anything changed: views compare it with what their draw was built from.
    uint64_t revision() const { return m_revision; }
    // Bumps when pixels in a level's atlas changed: a view drawing from it shows them only by rendering again.
    uint64_t revision(int level) const { return m_levelRevision[level]; }

    const TileSet& footprint(int level) { return m_pyramid.footprint(level); }   // tiles that may hold something
    // Up to date in the atlas (absent from it: empty).
    bool current(int level, TileKey key) const { return m_levels[level].current.contains(key); }
    // Builds and uploads tile `key` of `level`, unless current. False when the budget ran out first.
    bool build(CanvasFrame& frame, int level, TileKey key);
    const TileAtlas& atlas(int level) const { return m_levels[level].atlas; }

    size_t tileCount() const;   // on the GPU, every level
    size_t pageCount() const;
    void release();             // GPU resources and built levels; the next sync() starts over

private:
    struct Level {
        TileAtlas atlas;     // current, or stale until rebuilt
        TileSet   current;
    };

    void resetLevels();
    void invalidate(TileKey key);
    void touchAll();

    Node*       m_source = nullptr;
    std::shared_ptr<TileWatch> m_watch;   // the source's output changes
    TilePyramid m_pyramid;
    AtlasPages  m_pages;   // every level's tiles; before the levels, which free into it
    Level       m_levels[kLevels];
    RenderCtx   m_render;
    bool        m_fullSync = true;   // re-read the whole source first
    uint64_t    m_revision = 1;
    uint64_t    m_levelRevision[kLevels] = {};
};
