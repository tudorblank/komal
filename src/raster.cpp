#include "raster.h"
#include "gldevice.h"

// ---chunk---
// local data
RGBA& Chunk::pixel(int lx, int ly)
{
    return data[ly * SIZE + lx];
}
void Chunk::recomputeLocalBounds()
{
    localPixelBounds.reset();

    for(int ly = 0; ly < SIZE; ly++)
        for(int lx = 0; lx < SIZE; lx++)
            if(pixel(lx, ly).a > 0)
                localPixelBounds.expand(lx, ly);

    localPixelBounds.dirty = false;
}
// return atlas
Atlas& Chunk::getAtlas() const
{
    return rasterOwner->m_atlasPages[atlasPage];
}

// ---atlas---
Atlas::Atlas(Atlas&& o) noexcept
    : texID(o.texID), usedSlots(o.usedSlots)
{
    o.texID = 0;
    o.initialized = false;
}
Atlas& Atlas::operator=(Atlas&& o) noexcept
{
    if(this != &o)
    {
        if(texID) GLDevice::deleteTexture(texID);
        texID = o.texID;
        usedSlots = o.usedSlots;
        initialized = o.initialized;
        o.texID = 0;
        o.initialized = false;
    }
    return *this;
}
Atlas::~Atlas()
{
    if(texID) GLDevice::deleteTexture(texID);
}
void Atlas::assignSlot(Chunk& chunk, int pageIndex)
{
    chunk.atlasPage = pageIndex;
    chunk.atlasSlotX = usedSlots % SLOTS_PER_ROW; // [0] = col 0; [64] = col 0
    chunk.atlasSlotY = usedSlots / SLOTS_PER_ROW; // [<64] = row 0; 64 = row 1
    usedSlots++;
}

//// raster data
// ---chunk---
uint64_t RasterData::chunkKey(int chunkX, int chunkY) // coord key for chunk map
{ 
    return (static_cast<uint64_t>(chunkX) << 32) | static_cast<uint32_t>(chunkY);
}

bool RasterData::chunkExists(int chunkX, int chunkY) // find chunk in chunk map by its grid position [hash lookup]
{
    return m_chunks.count(chunkKey(chunkX, chunkY)) > 0;
}
Chunk& RasterData::accessChunk(int chunkX, int chunkY)
{
    uint64_t key = chunkKey(chunkX, chunkY);
    auto it = m_chunks.find(key);
    if(it != m_chunks.end())
        return it->second;

    return createChunk(chunkX, chunkY);
}
Chunk& RasterData::createChunk(int chunkX, int chunkY)
{
    uint64_t key = chunkKey(chunkX, chunkY);
    
    auto it = m_chunks.find(key);
    if(it != m_chunks.end())
        return it->second; // chunk already exists, return it
    
    // find atlas page with space, or create new one
    if(m_atlasPages.empty() || m_atlasPages.back().isFull())
        m_atlasPages.emplace_back();

    // update chunk internal data (pos on chunk grid + owner)
    auto [newIt, inserted] = m_chunks.try_emplace(key);
    Chunk& chunk = newIt->second;

    chunk.cPosX = chunkX;
    chunk.cPosY = chunkY;
    chunk.rasterOwner = this;

    // assign chunk to atlas page
    int pageIndex = static_cast<int>(m_atlasPages.size()) - 1;
    m_atlasPages[pageIndex].assignSlot(chunk, pageIndex);

    // update raster chunk bounds. only expands (since no chunks ever get deleted)
    // update when chunk get deleted for saving memory
    m_chunkBounds.expand(chunkX, chunkY); 

    return chunk;
}

// write chunk internal pixel data
void RasterData::setPixel(int worldX, int worldY, RGBA color)
{
    int chunkX = worldToChunk(worldX);
    int chunkY = worldToChunk(worldY);
    int pxLocalX = worldToLocal(worldX);
    int pxLocalY = worldToLocal(worldY);

    Chunk& accessedChunk = accessChunk(chunkX, chunkY);
    accessedChunk.pixel(pxLocalX, pxLocalY) = color;
    // store dirty chunk key for later GPU upload
    if(!accessedChunk.dirty)
    {
        accessedChunk.dirty = true;
        m_dirtyChunks.push_back(chunkKey(chunkX, chunkY));
    }

    // update raster pixel bbox + chunk local pixel bbox
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

    auto it = m_chunks.find(chunkKey(chunkX, chunkY));
    if(it == m_chunks.end()) return; // skip if chunk doesn't exist
    Chunk& accessedChunk = it->second;

    int pxLocalX = worldToLocal(worldX);
    int pxLocalY = worldToLocal(worldY);

    if(accessedChunk.pixel(pxLocalX, pxLocalY).a == 0) return; // skip if pixel already empty
    accessedChunk.pixel(pxLocalX, pxLocalY) = {0, 0, 0, 0};
    // store dirty chunk key for later GPU upload
    if(!accessedChunk.dirty)
    {
        accessedChunk.dirty = true;
        m_dirtyChunks.push_back(chunkKey(chunkX, chunkY));
    }

    markPixelErased(accessedChunk, pxLocalX, pxLocalY);
}
// check if chunk pixel bounds (local) or raster pixel bounds (global) need updating
void RasterData::markPixelErased(Chunk& chunk, int lx, int ly)
{
    if(!chunk.localPixelBounds.valid) return; // skip if chunk empty
    
    bool onLocalEdge =
        lx == chunk.localPixelBounds.minX || lx == chunk.localPixelBounds.maxX ||
        ly == chunk.localPixelBounds.minY || ly == chunk.localPixelBounds.maxY;

    if(!onLocalEdge) return; // interior pixel, this chunk's local bbox can't have shrunk

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

// re-derive of raster's global px bbox from local chunk px bbox
void RasterData::recomputePixelBounds()
{
    m_pixelBounds.reset();

    for(auto& [key, chunk] : m_chunks)
    {
        if(chunk.localPixelBounds.dirty)
            chunk.recomputeLocalBounds();

        if(!chunk.localPixelBounds.valid)
            continue; // skip empty chunks

        int worldMinX = chunkToWorld(chunk.cPosX, chunk.localPixelBounds.minX);
        int worldMaxX = chunkToWorld(chunk.cPosX, chunk.localPixelBounds.maxX);
        int worldMinY = chunkToWorld(chunk.cPosY, chunk.localPixelBounds.minY);
        int worldMaxY = chunkToWorld(chunk.cPosY, chunk.localPixelBounds.maxY);

        m_pixelBounds.expand(worldMinX, worldMinY);
        m_pixelBounds.expand(worldMaxX, worldMaxY);
    }

    m_pixelBounds.dirty = false;
}

// ---atlas---
void RasterData::flushDirtyGL(GLDevice& glDevice)
{
    // browse all chunks, find dirty ones, and update their atlas texture
    for(uint64_t key : m_dirtyChunks)
    {
        auto it = m_chunks.find(key);
        if(it == m_chunks.end()) continue; // skip if chunk doesn't exist
        
        Chunk& chunk = it->second;
        Atlas& atlas = m_atlasPages[chunk.atlasPage]; // get atlas for chunks

        if(!atlas.initialized)
            glDevice.initAtlasTexture(atlas);

        glDevice.uploadChunkToAtlas(chunk);
    }
    m_dirtyChunks.clear();
}
Atlas& RasterData::getAtlasForChunk(Chunk& chunk) { return m_atlasPages[chunk.atlasPage]; }