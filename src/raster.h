#pragma once
#include <unordered_map>
#include <vector>
#include <cstdint>

#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_3_Core>

class GLDevice;
class Atlas;
class RasterData;

struct RGBA { uint8_t r, g, b, a; };

class Chunk
{
public:
    static constexpr int SIZE = 64;
    RGBA data[SIZE * SIZE] = {};
    bool dirty = false;

    // chunk grid location
    int cPosX, cPosY;
    
    // atlas location
    int atlasPage = -1;  // which atlas texture
    int atlasSlotX = -1; // slot col
    int atlasSlotY = -1; // slot row

    // direct access to pixels in chunk
    RGBA& pixel(int lx, int ly);

    RasterData* rasterOwner = nullptr;
    Atlas& getAtlas() const;
};

class Atlas // GL texture pool that holds chunks
{
public:
    bool initialized = false;
    Atlas() {}

    // no copying
    Atlas(const Atlas&) = delete;
    Atlas& operator=(const Atlas&) = delete;

    // moving is fine
    Atlas(Atlas&& o) noexcept;
    Atlas& operator=(Atlas&& o) noexcept;
    ~Atlas();

    static constexpr int PAGE_SIZE = 4096;
    static constexpr int SLOTS_PER_ROW  = PAGE_SIZE / Chunk::SIZE; // 64
    static constexpr int SLOTS_TOTAL    = SLOTS_PER_ROW * SLOTS_PER_ROW; // 4096

    GLuint texID = 0;
    int usedSlots = 0;

    void assignSlot(Chunk& chunk, int pageIndex);
    bool isFull() { return usedSlots >= SLOTS_TOTAL; }
};

class RasterData
{
public:
    RasterData()  {}
    ~RasterData() {}

    std::unordered_map<uint64_t, Chunk> m_chunks;
    std::vector<Atlas> m_atlasPages;

    uint64_t chunkKey(int chunkX, int chunkY);

    // ---chunk related---
    bool chunkExists(int chunkX, int chunkY);
    Chunk& accessChunk(int chunkX, int chunkY);
    Chunk& createChunk(int chunkX, int chunkY);

    void setPixel(int worldX, int worldY, RGBA color);
    void erasePixel(int worldX, int worldY);

    // ---atlas stuff---
    void flushDirtyGL(GLDevice& glDevice);
    Atlas& getAtlasForChunk(Chunk& chunk);

    size_t cpuMemoryBytes() // each chunk is 64*64*4 = 16KB
    { return m_chunks.size() * sizeof(Chunk); }

    size_t gpuMemoryBytes() // each atlas page is 4096*4096*4 bytes = 64MB
    { return m_atlasPages.size() * Atlas::PAGE_SIZE * Atlas::PAGE_SIZE * 4; }
};