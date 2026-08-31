#pragma once

#include "raster-utils.hpp"

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <memory>

class Chunk{
public:
    RGBA data[Grid::CHUNK_SIZE * Grid::CHUNK_SIZE] = {};
    RGBA& pixel(int lx, int ly) { return data[ly * Grid::CHUNK_SIZE + lx]; } // pixel access
    const RGBA& pixel(int lx, int ly) const { return data[ly * Grid::CHUNK_SIZE + lx]; } // const pixel access (e.g. reading a cached const Chunk&)
    
    // occupancy
    int m_pixelCount = 0;
    bool isEmpty() const { return m_pixelCount == 0; }

    // chunk bounds
    BoundsI localBounds;
    void recomputeLocalBounds();
    const BoundsI& getLocalBounds() // chunk bounds getter
    {
        recomputeLocalBounds();
        return localBounds;
    }

};

class ChunkPool{
public:
    // blocks
    static constexpr uint32_t BLOCK_SIZE = 1024; // chunks per block
    std::vector<std::unique_ptr<Chunk[]>> m_blocks; // vector of pointers [chunk data]
    void ensureBlockFor(uint32_t idx);

    // indices
    std::vector<char> m_idxList; // 0 / 1 (validity)
    std::vector<uint32_t> m_freeList;
    uint32_t allocateIndex();
    void freeIndex(uint32_t idx);

    // chunk getter
    Chunk& getChunk(uint32_t idx)
    { return m_blocks[idx / BLOCK_SIZE][idx % BLOCK_SIZE]; }
};

class SparseRasterGrid{
public:
    static constexpr uint32_t EMPTY_SLOT = 0xFFFFFFFFu;

private:
    struct IndexPage
    {
        uint32_t m_slots[Grid::IDXPAGE_SIZE * Grid::IDXPAGE_SIZE];
        int liveCount = 0;
        IndexPage()
        { for(auto&s : m_slots) s = EMPTY_SLOT; }
    };

    ChunkPool m_pool;
    std::unordered_map<uint64_t, std::unique_ptr<IndexPage>> m_pages;
    // index cache
    uint64_t m_lastPageKey = ~0ull;
    IndexPage* m_lastPage = nullptr;

    std::vector<uint64_t> m_keysToEmptyPages;

    IndexPage* readPage(uint64_t inputKey);
    IndexPage& accessOrCreatePage(uint64_t inputKey);

public:
    void sweepEmptyPages();
    void clearAll(); // nuke

    Chunk* readChunk(int chunkX, int chunkY);
    Chunk& accessOrCreateChunk(int chunkX, int chunkY);
    void freeChunk(int chunkX, int chunkY);
    std::vector<Key::XY> listOccupiedChunks();
};

class RasterData{
public:
    RasterData() {}
    ~RasterData() {}

    SparseRasterGrid m_grid;

    // chunk wrappers
    Chunk* readChunk(int chunkX, int chunkY) { return m_grid.readChunk(chunkX, chunkY); }
    Chunk& accessChunk(int chunkX, int chunkY) { return m_grid.accessOrCreateChunk(chunkX, chunkY); }
    void freeChunk(int chunkX, int chunkY) { m_grid.freeChunk(chunkX, chunkY); }
    void sweepEmptyPages() { m_grid.sweepEmptyPages(); }

    // editing
    void setPixel(int worldX, int worldY, RGBA color);
    void erasePixel(int worldX, int worldY);
    void markPixelErased(Chunk& chunk, int chunkX, int chunkY, int lx, int ly);

    // bounds
    BoundsI m_pixelBounds;
    void recomputePixelBounds();
    BoundsI& getPixelBounds()
    {
        if(m_pixelBounds.dirty) recomputePixelBounds();
        return m_pixelBounds;
    }
};