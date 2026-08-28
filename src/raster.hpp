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
    // 64 x 64 RGBA pixels storage
    static constexpr int SIZE = 64;
    RGBA data[SIZE * SIZE] = {};
    bool dirty = false;
    RGBA& pixel(int lx, int ly) { return data[ly * SIZE + lx]; } // pixel access
    const RGBA& pixel(int lx, int ly) const { return data[ly * SIZE + lx]; } // const pixel access (e.g. reading a cached const Chunk&)
    
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

class Grid{
public:
    static inline int floorDiv(int x, int y)
    {
        if (y <= 0) return 0;
        if (x >= 0) return x / y;
        return - ( ( -x + y - 1 ) / y );
    }
    static inline int worldToChunk(int world) { return floorDiv(world, Chunk::SIZE); }
    static inline int worldToLocal(int world)
    {
        int cx = worldToChunk(world);
        return world - (cx * Chunk::SIZE);
    }
    static inline int chunkToWorld(int chunkCoord, int localCoord)
    { return chunkCoord * Chunk::SIZE + localCoord; }
};

class RasterData
{
public:
    RasterData() {}
    ~RasterData() {}

    ChunkPool m_chunkPool;
    std::unordered_map<uint64_t, uint32_t> m_chunkIndexMap; // key -> chunk index (pool)
    std::vector<uint64_t> m_dirtyChunkKeys; // used outside of class

    // chunk utilities
    Chunk* readChunk(int chunkX, int chunkY);
    Chunk& accessChunk(int chunkX, int chunkY);
    void freeChunk(int chunkX, int chunkY);

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