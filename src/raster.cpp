#include "raster.h"
#include "gldevice.h"

// chunk
RGBA& Chunk::pixel(int lx, int ly)
{
    return data[ly * SIZE + lx];
}
Atlas& Chunk::getAtlas() const
{
    return rasterOwner->m_atlasPages[atlasPage];
}

// atlas
Atlas::Atlas(Atlas&& o) noexcept : texID(o.texID), usedSlots(o.usedSlots)
{
    o.texID = 0; // prevent the moved-from object from deleting the texture
}
Atlas& Atlas::operator=(Atlas&& o) noexcept
{
    if(this != &o)
    {
        if(texID) GLDevice::deleteTexture(texID);
        texID = o.texID;
        usedSlots = o.usedSlots;
        o.texID = 0; // prevent the moved-from object from deleting the texture
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

bool RasterData::chunkExists(int chunkX, int chunkY) // find chunk in chunk map by its grid position
{
    return m_chunks.count(chunkKey(chunkX, chunkY)) > 0;
}
Chunk& RasterData::accessChunk(int chunkX, int chunkY) // access chunk in chunk map [rasterObject->chunk]
{
    if(!chunkExists(chunkX, chunkY))
        createChunk(chunkX, chunkY);
    
    return m_chunks[chunkKey(chunkX, chunkY)];
}
Chunk& RasterData::createChunk(int chunkX, int chunkY)
{
    // if chunk exists, exit function and return it
    if(chunkExists(chunkX, chunkY))
        return m_chunks[chunkKey(chunkX, chunkY)];
    
    // find atlas page with space, or create new one
    if(m_atlasPages.empty() || m_atlasPages.back().isFull())
    {
        m_atlasPages.emplace_back();
    }

    Chunk tempChunk;
    // update chunk internal data (pos on chunk grid + owner)
    tempChunk.cPosX = chunkX;
    tempChunk.cPosY = chunkY;
    tempChunk.rasterOwner = this;

    // assign chunk to atlas page
    int pageIndex = m_atlasPages.size() - 1;
    m_atlasPages[pageIndex].assignSlot(tempChunk, pageIndex);

    // assign chunk to raster chunk map
    m_chunks[chunkKey(chunkX, chunkY)] = tempChunk;
    return m_chunks[chunkKey(chunkX, chunkY)];
}

// write chunk internal pixel data
void RasterData::setPixel(int worldX, int worldY, RGBA color)
{
    int chunkX = worldX >> 6;
    int chunkY = worldY >> 6;
    int pxLocalX = worldX & 63;
    int pxLocalY = worldY & 63;

    Chunk& tempChunk = accessChunk(chunkX, chunkY);
    tempChunk.pixel(pxLocalX, pxLocalY) = color;
    tempChunk.dirty = true;
}
void RasterData::erasePixel(int worldX, int worldY)
{
    int chunkX = worldX >> 6;
    int chunkY = worldY >> 6;

    if(!chunkExists(chunkX, chunkY)) return; // chunk doesn't exist, nothing to erase

    int pxLocalX = worldX & 63;
    int pxLocalY = worldY & 63;

    Chunk& tempChunk = m_chunks[chunkKey(chunkX, chunkY)];
    tempChunk.pixel(pxLocalX, pxLocalY) = {0, 0, 0, 0};
    tempChunk.dirty = true;
}

// ---atlas---
void RasterData::flushDirtyGL(GLDevice& glDevice)
{
    // browse all chunks, find dirty ones, and update their atlas texture
    for(auto& [key, chunk] : m_chunks)
    {
        Atlas& atlas = m_atlasPages[chunk.atlasPage]; // get atlas for chunks

        if(!atlas.initialized)
        {
            glDevice.initAtlasTexture(atlas);
            atlas.initialized = true;
        }

        if(chunk.dirty)
            glDevice.uploadChunkToAtlas(chunk);
    }
}
Atlas& RasterData::getAtlasForChunk(Chunk& chunk)
{
    return m_atlasPages[chunk.atlasPage];
}