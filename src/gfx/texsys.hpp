#pragma once

#include <QMatrix4x4>

#include <webgpu.h>
#include "raster.hpp"
#include "gfxcontext.hpp"

#include <vector>
#include <unordered_map>
#include <cstdint>

struct TexVert{
    float pos[2];
    float uv[2];
};
struct ChunkUniform{
    float model[16];
    float uvOffset[2];
    float uvScale[2];
    float opacity = 1.0f;
    float _pad0[7] = {};
};

struct ChunkObject{
    float x, y, w, h;
};

struct ChunkInstance{
    float posX, posY;
    float scaleX, scaleY;
    float uvOffset[2];
    float uvScale[2];
    float opacity;
};

struct ChunkSlot{
    int page = -1;
    int slotX = 0, slotY = 0;
};

struct AtlasPage{
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
    WGPUBindGroup bindGroup = nullptr;
    WGPUBuffer instanceBuffer = nullptr;
    std::vector<bool> slotUsed; 
    int usedSlots = 0;
    static constexpr int PAGE_SIZE = 4096;
    static constexpr int SLOTS_PER_ROW = PAGE_SIZE / Chunk::SIZE;
    static constexpr int SLOTS_TOTAL = SLOTS_PER_ROW * SLOTS_PER_ROW;
    bool isFull() const { return usedSlots >= SLOTS_TOTAL; }
};

inline ChunkInstance makeChunkInstance(const ChunkObject& obj, const ChunkSlot& slot)
{
    ChunkInstance inst{};
    inst.posX = obj.x + obj.w * 0.5f;
    inst.posY = obj.y + obj.h * 0.5f;
    inst.scaleX = obj.w;
    inst.scaleY = obj.h;
    inst.uvOffset[0] = (float)(slot.slotX * Chunk::SIZE) / AtlasPage::PAGE_SIZE;
    inst.uvOffset[1] = (float)(slot.slotY * Chunk::SIZE) / AtlasPage::PAGE_SIZE;
    inst.uvScale[0] = (float)Chunk::SIZE / AtlasPage::PAGE_SIZE;
    inst.uvScale[1] = (float)Chunk::SIZE / AtlasPage::PAGE_SIZE;
    inst.opacity = 1.0f;
    return inst;
}

struct AtlasSet{
    std::vector<AtlasPage> pages;
    std::unordered_map<uint64_t, ChunkSlot> chunkSlots; // keyed by chunkKey
};

class TexSys{
public:
    GFXContext m_ctx;
    WGPUBuffer m_vertBuff = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr; // internal
    WGPURenderPipeline m_pipeline = nullptr;
    
    void createRenderPipeline(WGPUBindGroupLayout camLayout);

    //// atlas
    WGPUSampler m_atlasSampler = nullptr;
    void initAtlasSampler();
    std::vector<AtlasSet> m_atlases; // 1 atlas per layer
    AtlasSet& atlasForLayer(size_t layerIndex);
    AtlasPage createAtlasPage();
    ChunkSlot allocateSlot(AtlasSet& atlas, uint64_t chunkKey);
    void freeSlot(AtlasSet& atlas, uint64_t chunkKey);

    // chunk
    ChunkObject registerChunk(AtlasSet& atlas, uint64_t chunkKey, float x, float y, float w, float h, const RGBA* pixels);
    void writeChunkToAtlas(AtlasPage& page, ChunkSlot slot, const RGBA* pixels);
    std::vector<std::unordered_map<uint64_t, ChunkObject>> m_chunkObjects;

    void updateChunkObject(uint64_t chunkKey, ChunkObject& obj, AtlasSet& atlas, const RGBA* pixels);
    void syncChunk(size_t layerIndex, uint64_t chunkKey, float worldX, float worldY, const RGBA* pixels);

    // destructor
    void releaseAll();
    ~TexSys() { releaseAll(); }
};