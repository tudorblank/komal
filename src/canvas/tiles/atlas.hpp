#pragma once
// Raster tiles on the GPU: 64x64 tiles in slots of 2048x2048 pages (AtlasPages), found by key in
// an atlas (TileAtlas; several atlases can share pages), and drawn by each view that shows them
// with one instanced draw per page (TileDrawList, texture.wgsl, premultiplied). Owners decide
// which tiles go in; views decide where they're drawn.

#include "canvas/pipelines.hpp"
#include "gfx/slot-pages.hpp"
#include "raster/tile.hpp"

#include <absl/container/flat_hash_map.h>
#include <vector>

// Vertex buffer 0 of the atlas pipeline, one per visible tile (see texture.wgsl).
struct ChunkInstance {
    float center[2];
    float scale[2];
    float uvOffset[2];
    float uvScale[2];
    float opacity;
};

// Pages of tile slots (SlotPages), each with the bind group the atlas pipeline samples it
// through; shared by any number of atlases.
class AtlasPages {
public:
    using Slot = SlotPages::Slot;

    Slot allocate(const CanvasPipelines& p);
    void upload(const CanvasPipelines& p, Slot s, const RGBA* pixels);
    // Reused only after recycle(): a view that drew it this frame still reads it.
    void free(Slot s)    { m_slots.free(s); }
    void freeNow(Slot s) { m_slots.freeNow(s); }   // no view drew it this frame
    void recycle()       { m_slots.recycle(); }    // once a frame, before any view builds its draw
    void release();      // the GPU resources; the atlases on these pages must be cleared first

    size_t pageCount() const { return m_slots.pageCount(); }

private:
    friend class TileDrawList;

    // A 1 px gutter repeats each tile's edge, so sampling on a tile border never reads the next slot.
    static constexpr int kGutter     = 1;
    static constexpr int kSlotStride = Tile::kSize + 2 * kGutter;
    static constexpr int kPageSize   = 2048;   // 16 MB of RGBA8, 961 tiles

    SlotPages m_slots{ { .slotWidth = kSlotStride, .slotHeight = kSlotStride, .pageSize = kPageSize,
                         .format = WGPUTextureFormat_RGBA8Unorm,
                         .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst, .label = "Atlas Page" } };
    std::vector<GpuObject<WGPUBindGroup>> m_bindGroups;   // per page: its texture and the sampler
    std::vector<RGBA> m_staging;                          // tile + gutter, one upload per tile
};

// Tiles by key, in the slots of its own pages or shared ones.
class TileAtlas {
public:
    TileAtlas() = default;
    ~TileAtlas() { clear(); }
    TileAtlas(const TileAtlas&) = delete;
    TileAtlas& operator=(const TileAtlas&) = delete;

    void share(AtlasPages& pages) { m_pages = &pages; }   // while empty; they must outlive it

    void upload(const CanvasPipelines& p, TileKey key, const RGBA* pixels);   // adds or replaces
    void free(TileKey key);   // its slot is reused after the pages' next recycle()
    void clear();             // drops every tile; before any view draws from it this frame
    void release();           // clear(), and drops its own pages' GPU resources

    bool   contains(TileKey key) const { return m_chunks.contains(key); }
    bool   empty()     const { return m_chunks.empty(); }
    size_t tileCount() const { return m_chunks.size(); }
    const AtlasPages& pages() const { return *m_pages; }

private:
    friend class TileDrawList;

    AtlasPages  m_own;
    AtlasPages* m_pages = &m_own;
    absl::flat_hash_map<TileKey, AtlasPages::Slot> m_chunks;
};

// What one view draws from one atlas: between begin and end, add() draws tile `key` over the
// world square (x, y, size), or only its part (subX, subY, subSize), in 0..1 of the tile. end()
// uploads the pages whose list changed; true if any did.
class TileDrawList {
public:
    TileDrawList() = default;
    ~TileDrawList() { release(); }
    TileDrawList(const TileDrawList&) = delete;
    TileDrawList& operator=(const TileDrawList&) = delete;

    void begin(const TileAtlas& atlas);
    bool add(TileKey key, float x, float y, float size, float subX = 0.0f, float subY = 0.0f, float subSize = 1.0f);
    bool end(const CanvasPipelines& p, float opacity);
    // The atlas pipeline and a camera must be bound.
    void draw(WGPURenderPassEncoder pass) const;
    void release();
    bool empty() const;   // draws nothing

private:
    struct Page {
        GpuObject<WGPUBuffer> buffer;
        std::vector<ChunkInstance> instances;   // what the buffer holds now
    };
    const TileAtlas* m_atlas = nullptr;
    std::vector<Page> m_pages;
    std::vector<std::vector<ChunkInstance>> m_building;   // per page, between begin and end
};
