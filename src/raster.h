#pragma once
#include <stdio.h>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <utility>
#include <memory>

class RasterData;

struct Vec2{
    float x, y;
};

struct RGBA { uint8_t r, g, b, a; };

struct BoundsI
{
    int minX, minY, maxX, maxY;
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

class Chunk
{
public:
    static constexpr int SIZE = 64;
    RGBA data[SIZE * SIZE] = {};
    bool dirty = false;

    // chunk grid location
    int cPosX, cPosY;

    // direct access to pixels in chunk
    RGBA& pixel(int lx, int ly) { return data[ly * SIZE + lx]; }

    // local pixel bbox
    BoundsI localPixelBounds; // within chunk
    void recomputeLocalBounds(); // chunk rescan + clean dirty bbox
    const BoundsI& returnLocalBounds() // chunk local bounds getter, recompute if dirty
    {
        if(localPixelBounds.dirty)
            recomputeLocalBounds();
        return localPixelBounds;
    }

    RasterData* rasterOwner = nullptr;
};

class ChunkPool // pool of blocks of chunks
{
public:
    static constexpr uint32_t BLOCK_SIZE = 1024; // chunks per block (16KB * 1024 = 16MB/slab)

    struct Handle
    {
        static constexpr uint32_t kInvalid = 0xFFFFFFFF; // "null"
        uint32_t index = kInvalid;
        bool valid() const { return index != kInvalid; }
    };

    std::vector<bool> m_alive;
    std::vector<uint32_t> m_freeIndices;
    uint32_t m_indexCount = 0;

    Handle allocateHandle()
    {
        uint32_t idx;
        if(!m_freeIndices.empty())
        {
            idx = m_freeIndices.back();
            m_freeIndices.pop_back();
        }
        else
        {
            idx = m_indexCount++;
            ensureBlockFor(idx);
        }
        m_alive[idx] = true;
        return { idx };
    }
    void freeHandle(Handle h)
    {
        if(!h.valid() || !m_alive[h.index]) return;
        getChunk(h) = Chunk{};
        m_alive[h.index] = false;
        m_freeIndices.push_back(h.index);
    }

    std::vector<std::unique_ptr<Chunk[]>> m_blocks;
    void ensureBlockFor(uint32_t idx)
    {
        size_t blockIDX = idx / BLOCK_SIZE;
        while(m_blocks.size() <= blockIDX)
        {
            m_blocks.push_back(std::make_unique<Chunk[]>(BLOCK_SIZE));
            m_alive.resize(m_blocks.size() * BLOCK_SIZE, false);
        }
    }
    Chunk& getChunk(Handle h)
    {
        return m_blocks[h.index / BLOCK_SIZE][h.index % BLOCK_SIZE];
    }
    template<typename Fn>
    void forEachAlive(Fn&& fn)
    {
        for(uint32_t i = 0; i < m_indexCount; i++)
            if(m_alive[i])
                fn(getChunk({i}));
    }

    size_t aliveChunkCount() const { return (size_t)m_indexCount - m_freeIndices.size(); }
    size_t bytesAllocated() const { return (size_t)m_blocks.size() * BLOCK_SIZE * sizeof(Chunk); }
};

class RasterData
{
public:
    RasterData()  {}
    ~RasterData() {}
    RasterData(const RasterData&) = delete;
    RasterData& operator=(const RasterData&) = delete;
    RasterData(RasterData&& other) noexcept;
    RasterData& operator=(RasterData&& other) noexcept;

    ChunkPool m_chunkPool;

    std::unordered_map<uint64_t, ChunkPool::Handle> m_chunkHandleMap;
    uint64_t chunkKey(int chunkX, int chunkY);
    std::vector<uint64_t> m_dirtyChunkKeys;

    bool chunkExists(int chunkX, int chunkY);
    Chunk& accessChunk(int chunkX, int chunkY);
    Chunk& createChunk(int chunkX, int chunkY);
    Chunk* tryGetChunk(int chunkX, int chunkY);

    template<typename Fn>
    void forEachChunk(Fn&& fn) { m_chunkPool.forEachAlive(std::forward<Fn>(fn)); }

    static int worldToChunk(int world) { return world >> 6; }
    static int worldToLocal(int world) { return world & 63; }
    static int chunkToWorld(int chunkCoord, int localCoord) { return (chunkCoord << 6) + localCoord; }

    void setPixel(int worldX, int worldY, RGBA color);
    void erasePixel(int worldX, int worldY);
    void markPixelErased(Chunk& chunk, int lx, int ly);

    BoundsI m_chunkBounds;
    BoundsI m_pixelBounds;
    void recomputePixelBounds();
    BoundsI& returnPixelBounds()
    {
        if(m_pixelBounds.dirty)
            recomputePixelBounds();
        return m_pixelBounds;
    }
};