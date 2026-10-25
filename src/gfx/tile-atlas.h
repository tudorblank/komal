#pragma once
// Raster tiles on the GPU: 64x64 tiles kept in slots of 2048x2048 atlas pages, drawn with one
// instanced call per page (texture.wgsl, premultiplied). Owners decide which tiles go in;
// the atlas only stores, positions and draws them.

#include "gfx/canvas-pipelines.h"
#include "raster/tile.h"

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

class TileAtlas {
public:
    TileAtlas() = default;
    ~TileAtlas() { release(); }
    TileAtlas(const TileAtlas&) = delete;
    TileAtlas& operator=(const TileAtlas&) = delete;

    void upload(const CanvasPipelines& p, TileKey key, const RGBA* pixels);   // adds or replaces
    void free(TileKey key);
    void clear();     // drops every tile; pages are kept for reuse
    void release();   // drops the GPU resources too

    // Instances for the tiles inside the tile range (inclusive), rebuilt only when tiles came
    // or went, the range or the opacity changed; only pages whose list changed are uploaded.
    // True if what draw() shows changed.
    bool updateInstances(const CanvasPipelines& p, int minTX, int minTY, int maxTX, int maxTY, float opacity);

    // Or a hand-made list, for owners that place tiles themselves (levels of detail): between
    // begin and end, add() draws tile `key` over the world square (x, y, size), or only the
    // part (subX, subY, subSize) of it, in 0..1 of the tile. end() uploads the pages whose list
    // changed and returns true if anything did.
    void beginInstances();
    bool add(TileKey key, float x, float y, float size, float subX = 0.0f, float subY = 0.0f, float subSize = 1.0f);
    bool endInstances(const CanvasPipelines& p, float opacity);
    // The atlas pipeline and a camera must be bound.
    void draw(WGPURenderPassEncoder pass) const;

    bool   contains(TileKey key) const { return m_chunks.contains(key); }
    bool   empty()     const { return m_chunks.empty(); }
    size_t tileCount() const { return m_chunks.size(); }
    size_t pageCount() const { return m_pages.size(); }

private:
    // Each slot has a 1 px gutter repeating the tile's edge pixels, so sampling exactly on a
    // tile border never picks up the neighbouring slot.
    static constexpr int kGutter      = 1;
    static constexpr int kPageSize    = 2048;   // 16 MB of RGBA8, 961 tiles
    static constexpr int kSlotStride  = Tile::kSize + 2 * kGutter;
    static constexpr int kSlotsPerRow = kPageSize / kSlotStride;
    static constexpr int kSlotsTotal  = kSlotsPerRow * kSlotsPerRow;

    struct Page {
        WGPUTexture     texture        = nullptr;
        WGPUTextureView view           = nullptr;
        WGPUBindGroup   bindGroup      = nullptr;   // texture + sampler
        WGPUBuffer      instanceBuffer = nullptr;
        std::vector<ChunkInstance> instances;       // what the instance buffer holds now
        std::vector<uint16_t> freeSlots;            // popped from the back: low slots first
    };
    struct Chunk {
        int tx, ty;
        int page, slot;
    };

    Page createPage(const CanvasPipelines& p);
    ChunkInstance makeInstance(const Chunk& c) const;
    bool uploadInstances(const CanvasPipelines& p, std::vector<std::vector<ChunkInstance>>& next);

    std::vector<Page> m_pages;
    absl::flat_hash_map<TileKey, Chunk> m_chunks;
    std::vector<RGBA> m_staging;    // tile + gutter, one upload per tile
    std::vector<std::vector<ChunkInstance>> m_building;   // per page, between begin/endInstances
    float m_opacity     = 1.0f;
    bool  m_layoutDirty = true;     // tiles added/removed: instances are stale
    int   m_minTX = 1, m_maxTX = 0, m_minTY = 1, m_maxTY = 0;   // tile range the instances cover
};
