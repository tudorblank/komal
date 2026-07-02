#pragma once
#include <unordered_map>
#include <vector>
#include <cstdint>

#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_3_Core>

class GLDevice;
class Atlas;
class RasterData;

struct Vec2
{
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
    
    // atlas location
    int atlasPage = -1;  // which atlas texture
    int atlasSlotX = -1; // slot col
    int atlasSlotY = -1; // slot row

    // direct access to pixels in chunk
    RGBA& pixel(int lx, int ly);

    // local pixel bbox
    BoundsI localPixelBounds; // within chunk
    void recomputeLocalBounds(); // chunk rescan + clean dirty bbox
    const BoundsI& returnLocalBounds()
    {
        if(localPixelBounds.dirty)
            recomputeLocalBounds();
        return localPixelBounds;

    }

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
    uint64_t chunkKey(int chunkX, int chunkY);
    std::vector<uint64_t> m_dirtyChunks;

    std::vector<Atlas> m_atlasPages;

    // ---chunk related---
    bool chunkExists(int chunkX, int chunkY);
    Chunk& accessChunk(int chunkX, int chunkY);
    Chunk& createChunk(int chunkX, int chunkY);
    // converters
    static int worldToChunk(int world) { return world >> 6; }
    static int worldToLocal(int world) { return world & 63; }
    static int chunkToWorld(int chunkCoord, int localCoord) { return (chunkCoord << 6) + localCoord; }
    // edit chunk
    void setPixel(int worldX, int worldY, RGBA color);
    void erasePixel(int worldX, int worldY);
    void markPixelErased(Chunk& chunk, int lx, int ly); // alpha > 0 -> 0
    // bboxes
    BoundsI m_chunkBounds;
    BoundsI m_pixelBounds;
    void recomputePixelBounds(); // calc raster's pixel bounds
    BoundsI& returnPixelBounds()
    {
        if(m_pixelBounds.dirty)
            recomputePixelBounds();
        
        return m_pixelBounds;
    }

    // ---atlas stuff---
    void flushDirtyGL(GLDevice& glDevice);
    Atlas& getAtlasForChunk(Chunk& chunk);

    // ---raster memory usage---
    size_t cpuMemoryBytes() // each chunk is 64*64*4 = 16KB
    { return m_chunks.size() * sizeof(Chunk); }

    size_t gpuMemoryBytes() // each atlas page is 4096*4096*4 bytes = 64MB
    { return m_atlasPages.size() * Atlas::PAGE_SIZE * Atlas::PAGE_SIZE * 4; }
};