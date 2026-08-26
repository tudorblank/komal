#include "raster.hpp"

// ==== CHUNK ====
void Chunk::recomputeLocalBounds()
{
    if(!localBounds.dirty) return;

    localBounds.reset();

    for(int ly = 0; ly < SIZE; ly++)
        for(int lx = 0; lx < SIZE; lx++)
            if(pixel(lx, ly).a > 0)
                localBounds.expand(lx, ly);

    localBounds.dirty = false;
}

// ==== CHUNK POOL ====
void ChunkPool::ensureBlockFor(uint32_t idx)
{
    size_t blockIDX = idx / BLOCK_SIZE;
    while(m_blocks.size() <= blockIDX)
        m_blocks.push_back(std::make_unique<Chunk[]>(BLOCK_SIZE));
}
uint32_t ChunkPool::allocateIndex()
{
    uint32_t idx;

    // grab from free ones (if available)
    if(!m_freeList.empty())
    {
        idx = m_freeList.back();
        m_freeList.pop_back();
        m_idxList[idx] = true;
        return idx;
    }

    idx = (uint32_t)m_idxList.size();
    ensureBlockFor(idx);
    m_idxList.push_back(true);
    return idx;
}
void ChunkPool::freeIndex(uint32_t idx)
{
    if(idx >= m_idxList.size() || !m_idxList[idx]) return; // doesn't exist / already free

    getChunk(idx) = Chunk{}; // reset chunk
    m_idxList[idx] = false;
    m_freeList.push_back(idx);
}

// ==== RASTER DATA ====
Chunk* RasterData::readChunk(int chunkX, int chunkY)
{
    uint64_t key = Key::pack(chunkX, chunkY);
    auto it = m_chunkIndexMap.find(key); // search it by key

    if(it == m_chunkIndexMap.end()) return nullptr; // not found
    return &m_chunkPool.getChunk(it->second);
}
Chunk& RasterData::accessChunk(int chunkX, int chunkY)
{
    uint64_t key = Key::pack(chunkX, chunkY);
    auto it = m_chunkIndexMap.find(key);

    if(it != m_chunkIndexMap.end()) return m_chunkPool.getChunk(it->second); // found

    // not found -> create
    uint32_t idx = m_chunkPool.allocateIndex(); 
    m_chunkIndexMap.emplace(key, idx);
    return m_chunkPool.getChunk(idx);
}
void RasterData::freeChunk(int chunkX, int chunkY)
{
    uint64_t key = Key::pack(chunkX, chunkY);
    auto it = m_chunkIndexMap.find(key);
    if(it == m_chunkIndexMap.end()) return; // wasn't allocated, nothing to do

    m_chunkPool.freeIndex(it->second);
    m_chunkIndexMap.erase(it);

    std::erase(m_dirtyChunkKeys, key);
}

void RasterData::setPixel(int worldX, int worldY, RGBA color)
{
    if(color.a == 0)
    {
        erasePixel(worldX, worldY);
        return;
    }
    
    // convert coords
    int chunkX = Grid::worldToChunk(worldX);
    int chunkY = Grid::worldToChunk(worldY);
    int pxLocalX = Grid::worldToLocal(worldX);
    int pxLocalY = Grid::worldToLocal(worldY);

    // set pixel
    Chunk& accessedChunk = accessChunk(chunkX, chunkY);
    accessedChunk.pixel(pxLocalX, pxLocalY) = color;

    // dirty
    if(!accessedChunk.dirty)
    {
        accessedChunk.dirty = true;
        m_dirtyChunkKeys.push_back(Key::pack(chunkX, chunkY));
    }
    
    // bounds
    accessedChunk.localBounds.expand(pxLocalX, pxLocalY); // local chunk
    m_pixelBounds.expand(worldX, worldY);
}
void RasterData::erasePixel(int worldX, int worldY)
{
    // convert coords
    int chunkX = Grid::worldToChunk(worldX);
    int chunkY = Grid::worldToChunk(worldY);
    int pxLocalX = Grid::worldToLocal(worldX);
    int pxLocalY = Grid::worldToLocal(worldY);

    Chunk* chunk = readChunk(chunkX, chunkY);
    if(!chunk) return;

    if(chunk->pixel(pxLocalX, pxLocalY).a == 0) return;
    chunk->pixel(pxLocalX, pxLocalY) = {0, 0, 0, 0};

    if(!chunk->dirty)
    {
        chunk->dirty = true;
        m_dirtyChunkKeys.push_back(Key::pack(chunkX, chunkY));
    }

    markPixelErased(*chunk, chunkX, chunkY, pxLocalX, pxLocalY);
}
void RasterData::markPixelErased(Chunk& chunk, int chunkX, int chunkY, int lx, int ly)
{
    const BoundsI& local = chunk.getLocalBounds();
    if(!local.valid) return;

    bool onLocalEdge =
        lx == local.minX || lx == local.maxX ||
        ly == local.minY || ly == local.maxY;

    if(!onLocalEdge) return; // interior pixel, no change

    chunk.localBounds.dirty = true;

    int worldX = Grid::chunkToWorld(chunkX, lx);
    int worldY = Grid::chunkToWorld(chunkY, ly);

    int worldMinX = Grid::chunkToWorld(chunkX, local.minX);
    int worldMaxX = Grid::chunkToWorld(chunkX, local.maxX);
    int worldMinY = Grid::chunkToWorld(chunkY, local.minY);
    int worldMaxY = Grid::chunkToWorld(chunkY, local.maxY);

    bool onGlobalEdge =
        (worldX == m_pixelBounds.minX && worldMinX == m_pixelBounds.minX) ||
        (worldX == m_pixelBounds.maxX && worldMaxX == m_pixelBounds.maxX) ||
        (worldY == m_pixelBounds.minY && worldMinY == m_pixelBounds.minY) ||
        (worldY == m_pixelBounds.maxY && worldMaxY == m_pixelBounds.maxY);

    if(onGlobalEdge) m_pixelBounds.dirty = true; // needs global rescan
}

void RasterData::recomputePixelBounds()
{
    m_pixelBounds.reset();

    for(auto& [key, idx] : m_chunkIndexMap)
    {
        Chunk& chunk = m_chunkPool.getChunk(idx);
        const BoundsI& local = chunk.getLocalBounds();
        if(!local.valid) continue; // empty chunk, skip

        // convert coords
        Key::XY pos = Key::unpack(key);
        int worldMinX = Grid::chunkToWorld(pos.x, local.minX);
        int worldMaxX = Grid::chunkToWorld(pos.x, local.maxX);
        int worldMinY = Grid::chunkToWorld(pos.y, local.minY);
        int worldMaxY = Grid::chunkToWorld(pos.y, local.maxY);

        m_pixelBounds.expand(worldMinX, worldMinY);
        m_pixelBounds.expand(worldMaxX, worldMaxY);
    }

    m_pixelBounds.dirty = false; // cleared
}