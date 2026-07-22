#include "raster.h"

void Chunk::recomputeLocalBounds()
{
    localPixelBounds.reset();

    for(int ly = 0; ly < SIZE; ly++)
        for(int lx = 0; lx < SIZE; lx++)
            if(pixel(lx, ly).a > 0)
                localPixelBounds.expand(lx, ly);

    localPixelBounds.dirty = false;
}

RasterData::RasterData(RasterData&& other) noexcept
    : m_chunkPool(std::move(other.m_chunkPool))
    , m_chunkHandleMap(std::move(other.m_chunkHandleMap))
    , m_dirtyChunkKeys(std::move(other.m_dirtyChunkKeys))
    , m_chunkBounds(other.m_chunkBounds)
    , m_pixelBounds(other.m_pixelBounds)
{
    forEachChunk([&](Chunk& chunk) { chunk.rasterOwner = this; });
}

RasterData& RasterData::operator=(RasterData&& other) noexcept
{
    if(this != &other)
    {
        m_chunkPool      = std::move(other.m_chunkPool);
        m_chunkHandleMap = std::move(other.m_chunkHandleMap);
        m_dirtyChunkKeys = std::move(other.m_dirtyChunkKeys);
        m_chunkBounds    = other.m_chunkBounds;
        m_pixelBounds    = other.m_pixelBounds;

        forEachChunk([&](Chunk& chunk) { chunk.rasterOwner = this; });
    }
    return *this;
}

uint64_t RasterData::chunkKey(int chunkX, int chunkY)
{
    return (static_cast<uint64_t>(chunkX) << 32) | static_cast<uint32_t>(chunkY);
}

bool RasterData::chunkExists(int chunkX, int chunkY)
{
    return m_chunkHandleMap.count(chunkKey(chunkX, chunkY)) > 0;
}
Chunk& RasterData::accessChunk(int chunkX, int chunkY)
{
    uint64_t key = chunkKey(chunkX, chunkY);
    auto it = m_chunkHandleMap.find(key);
    if(it != m_chunkHandleMap.end())
        return m_chunkPool.getChunk(it->second);

    return createChunk(chunkX, chunkY);
}
Chunk* RasterData::tryGetChunk(int chunkX, int chunkY)
{
    auto it = m_chunkHandleMap.find(chunkKey(chunkX, chunkY));
    return (it != m_chunkHandleMap.end()) ? &m_chunkPool.getChunk(it->second) : nullptr;
}

Chunk& RasterData::createChunk(int chunkX, int chunkY)
{
    uint64_t key = chunkKey(chunkX, chunkY);

    auto it = m_chunkHandleMap.find(key);
    if(it != m_chunkHandleMap.end())
        return m_chunkPool.getChunk(it->second);

    ChunkPool::Handle handle = m_chunkPool.allocateHandle();
    m_chunkHandleMap.emplace(key, handle);

    Chunk& chunk = m_chunkPool.getChunk(handle);
    chunk.cPosX = chunkX;
    chunk.cPosY = chunkY;
    chunk.rasterOwner = this;

    m_chunkBounds.expand(chunkX, chunkY);

    return chunk;
}

void RasterData::setPixel(int worldX, int worldY, RGBA color)
{
    int chunkX = worldToChunk(worldX);
    int chunkY = worldToChunk(worldY);
    int pxLocalX = worldToLocal(worldX);
    int pxLocalY = worldToLocal(worldY);

    Chunk& accessedChunk = accessChunk(chunkX, chunkY);
    accessedChunk.pixel(pxLocalX, pxLocalY) = color;

    if(!accessedChunk.dirty)
    {
        accessedChunk.dirty = true;
        m_dirtyChunkKeys.push_back(chunkKey(chunkX, chunkY));
    }

    if(color.a > 0)
    {
        accessedChunk.localPixelBounds.expand(pxLocalX, pxLocalY);
        m_pixelBounds.expand(worldX, worldY);
    }
    else markPixelErased(accessedChunk, pxLocalX, pxLocalY);
}
void RasterData::erasePixel(int worldX, int worldY)
{
    int chunkX = worldToChunk(worldX);
    int chunkY = worldToChunk(worldY);
    int pxLocalX = worldToLocal(worldX);
    int pxLocalY = worldToLocal(worldY);

    auto it = m_chunkHandleMap.find(chunkKey(chunkX, chunkY));
    if(it == m_chunkHandleMap.end()) return;
    Chunk& accessedChunk = m_chunkPool.getChunk(it->second);

    if(accessedChunk.pixel(pxLocalX, pxLocalY).a == 0) return;
    accessedChunk.pixel(pxLocalX, pxLocalY) = {0, 0, 0, 0};

    if(!accessedChunk.dirty)
    {
        accessedChunk.dirty = true;
        m_dirtyChunkKeys.push_back(chunkKey(chunkX, chunkY));
    }

    markPixelErased(accessedChunk, pxLocalX, pxLocalY);
}
void RasterData::markPixelErased(Chunk& chunk, int lx, int ly)
{
    if(!chunk.localPixelBounds.valid) return;

    bool onLocalEdge =
        lx == chunk.localPixelBounds.minX || lx == chunk.localPixelBounds.maxX ||
        ly == chunk.localPixelBounds.minY || ly == chunk.localPixelBounds.maxY;

    if(!onLocalEdge) return;

    chunk.localPixelBounds.dirty = true;

    int worldX = chunkToWorld(chunk.cPosX, lx);
    int worldY = chunkToWorld(chunk.cPosY, ly);

    int worldMinX = chunkToWorld(chunk.cPosX, chunk.localPixelBounds.minX);
    int worldMaxX = chunkToWorld(chunk.cPosX, chunk.localPixelBounds.maxX);
    int worldMinY = chunkToWorld(chunk.cPosY, chunk.localPixelBounds.minY);
    int worldMaxY = chunkToWorld(chunk.cPosY, chunk.localPixelBounds.maxY);

    bool onGlobalEdge =
        (worldX == m_pixelBounds.minX && worldMinX == m_pixelBounds.minX) ||
        (worldX == m_pixelBounds.maxX && worldMaxX == m_pixelBounds.maxX) ||
        (worldY == m_pixelBounds.minY && worldMinY == m_pixelBounds.minY) ||
        (worldY == m_pixelBounds.maxY && worldMaxY == m_pixelBounds.maxY);

    if(onGlobalEdge)
        m_pixelBounds.dirty = true;
}

void RasterData::recomputePixelBounds()
{
    m_pixelBounds.reset();

    forEachChunk([&](Chunk& chunk)
    {
        if(chunk.localPixelBounds.dirty)
            chunk.recomputeLocalBounds();

        if(!chunk.localPixelBounds.valid)
            return;

        int worldMinX = chunkToWorld(chunk.cPosX, chunk.localPixelBounds.minX);
        int worldMaxX = chunkToWorld(chunk.cPosX, chunk.localPixelBounds.maxX);
        int worldMinY = chunkToWorld(chunk.cPosY, chunk.localPixelBounds.minY);
        int worldMaxY = chunkToWorld(chunk.cPosY, chunk.localPixelBounds.maxY);

        m_pixelBounds.expand(worldMinX, worldMinY);
        m_pixelBounds.expand(worldMaxX, worldMaxY);
    });

    m_pixelBounds.dirty = false;
}