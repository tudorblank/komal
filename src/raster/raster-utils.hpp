#pragma once

#include <cstdint>

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