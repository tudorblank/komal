#pragma once

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <memory>

// commons
struct Vec2{
    float x, y;
};

struct RGBA{
    uint8_t r, g, b, a;
};
inline RGBA transparent() { return {0, 0, 0, 0}; }

class Grid{
public:
    static inline int floorDiv(int x, int y)
    {
        if (y <= 0) return 0;
        if (x >= 0) return x / y;
        return - ( ( -x + y - 1 ) / y );
    }

    static constexpr int CHUNK_SIZE = 64;
    static inline int worldToChunk(int world) { return floorDiv(world, CHUNK_SIZE); }
    static inline int worldToChunkLocal(int world)
    {
        int cx = worldToChunk(world);
        return world - (cx * CHUNK_SIZE);
    }
    static inline int chunkToWorld(int chunkCoord, int localCoord)
    { return chunkCoord * CHUNK_SIZE + localCoord; }

    static constexpr int IDXPAGE_SIZE = 16;
    static inline int chunkToPage(int chunkCoord) { return floorDiv(chunkCoord, IDXPAGE_SIZE); }
    static inline int chunkToPageLocal(int chunkCoord)
    {
        int pc = chunkToPage(chunkCoord);
        return chunkCoord - (pc * IDXPAGE_SIZE);
    }
};

class Key{
public:
    // pack key
    static inline uint64_t pack(int x, int y)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |  static_cast<uint32_t>(y);
    }
    // unpack key
    struct XY { int32_t x, y; };
    static inline XY unpack(uint64_t key)
    {
        return {
            static_cast<int32_t>(static_cast<uint32_t>(key >> 32)),
            static_cast<int32_t>(static_cast<uint32_t>(key & 0xFFFFFFFFu))
        };
    }
};

struct BoundsI{
    int minX = 0, minY = 0, maxX = -1, maxY = -1;
    bool valid = false;
    bool dirty = false;

    void reset()
    {
        valid = false;
        minX = minY = 0;
        maxX = maxY = -1;
    }
    void expand(int x, int y)
    {
        if(!valid)
        {
            minX = maxX = x;
            minY = maxY = y;
            valid = true;
        }
        else
        {
            if(x < minX) minX = x;
            if(x > maxX) maxX = x;
            if(y < minY) minY = y;
            if(y > maxY) maxY = y;
        }
    }

    int width()  const { return valid ? (maxX - minX + 1) : 0; }
    int height() const { return valid ? (maxY - minY + 1) : 0; }
    int posX()  const { return minX; }
    int posY()  const { return minY; }
};

// members
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