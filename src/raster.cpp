#include "raster.hpp"

// ==== CHUNK ====
void Chunk::recomputeLocalBounds()
{
    if(!localBounds.dirty) return;

    localBounds.reset();

    for(int ly = 0; ly < Grid::CHUNK_SIZE; ly++)
        for(int lx = 0; lx < Grid::CHUNK_SIZE; lx++)
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

// ==== SPARSE RASTER GRID ====
// index page functions
SparseRasterGrid::IndexPage* SparseRasterGrid::readPage(uint64_t inputKey)
{
    if(inputKey == m_lastPageKey && m_lastPage) return m_lastPage; // cache hit

    auto it = m_pages.find(inputKey);
    if(it == m_pages.end()) return nullptr; // not found => nothing

    // update cache
    m_lastPageKey = inputKey;
    m_lastPage = it->second.get();
    return m_lastPage;
}
SparseRasterGrid::IndexPage& SparseRasterGrid::accessOrCreatePage(uint64_t inputKey)
{
    if(inputKey == m_lastPageKey && m_lastPage) return *m_lastPage; // cache hit

    auto it = m_pages.find(inputKey);
    if(it == m_pages.end()) // not found => create
        it = m_pages.emplace(inputKey, std::make_unique<IndexPage>()).first;

    // update cache
    m_lastPageKey = inputKey;
    m_lastPage = it->second.get();
    return *m_lastPage;
}
void SparseRasterGrid::sweepEmptyPages()
{
    for(uint64_t key : m_keysToEmptyPages)
    {
        auto it = m_pages.find(key);
        if(it == m_pages.end()) continue; // not found
        if(it->second->liveCount != 0) continue; // repopulated before sweep

        if(key == m_lastPageKey) { m_lastPageKey = ~0ull; m_lastPage = nullptr; } // no dangling cache
        m_pages.erase(it);
    }
    m_keysToEmptyPages.clear();
}
void SparseRasterGrid::clearAll()
{
    // free pool
    for(auto& [pageKey, page] : m_pages)
        for(uint32_t idx : page->m_slots)
            if(idx != EMPTY_SLOT)
                m_pool.freeIndex(idx);
    // free indexes
    m_pages.clear();
    m_keysToEmptyPages.clear();
    // free cache
    m_lastPageKey = ~0ull;
    m_lastPage = nullptr;
}
// chunk functions
Chunk* SparseRasterGrid::readChunk(int chunkX, int chunkY)
{
    int pageX = Grid::chunkToPage(chunkX);
    int pageY = Grid::chunkToPage(chunkY);

    IndexPage* page = readPage(Key::pack(pageX, pageY));
    if(!page) return nullptr;

    int lx = Grid::chunkToPageLocal(chunkX);
    int ly = Grid::chunkToPageLocal(chunkY);

    uint32_t idx = page->m_slots[ly * Grid::IDXPAGE_SIZE + lx];
    return (idx == EMPTY_SLOT) ? nullptr : &m_pool.getChunk(idx);
}
Chunk& SparseRasterGrid::accessOrCreateChunk(int chunkX, int chunkY)
{
    int pageX = Grid::chunkToPage(chunkX);
    int pageY = Grid::chunkToPage(chunkY);

    IndexPage& page = accessOrCreatePage(Key::pack(pageX, pageY));

    int lx = Grid::chunkToPageLocal(chunkX);
    int ly = Grid::chunkToPageLocal(chunkY);

    uint32_t& slot = page.m_slots[ly * Grid::IDXPAGE_SIZE + lx];
    if(slot == EMPTY_SLOT)
    {
        slot = m_pool.allocateIndex();
        page.liveCount++;
    }
    return m_pool.getChunk(slot);
}
void SparseRasterGrid::freeChunk(int chunkX, int chunkY)
{
    int pageX = Grid::chunkToPage(chunkX);
    int pageY = Grid::chunkToPage(chunkY);

    uint64_t pageKey = Key::pack(pageX, pageY);
    IndexPage* page = readPage(pageKey);
    if(!page) return;

    int lx = Grid::chunkToPageLocal(chunkX);
    int ly = Grid::chunkToPageLocal(chunkY);

    uint32_t& slot = page->m_slots[ly * Grid::IDXPAGE_SIZE + lx];
    if(slot == EMPTY_SLOT) return;

    m_pool.freeIndex(slot);
    slot = EMPTY_SLOT;
    page->liveCount--;

    if(page->liveCount == 0)
        m_keysToEmptyPages.push_back(pageKey); // queue
}
std::vector<Key::XY> SparseRasterGrid::listOccupiedChunks()
{
    std::vector<Key::XY> result;

    for(auto& [pageKey, page] : m_pages)
    {
        Key::XY pagePos = Key::unpack(pageKey);

        for(int ly = 0; ly < Grid::IDXPAGE_SIZE; ly++)
            for(int lx = 0; lx < Grid::IDXPAGE_SIZE; lx++)
            {
                uint32_t idx = page->m_slots[ly * Grid::IDXPAGE_SIZE + lx];
                if(idx == EMPTY_SLOT) continue; // no chunk allocated -> skip

                Chunk& chunk = m_pool.getChunk(idx);
                if(chunk.isEmpty()) continue;   // a chunk exists, but empty -> skip

                int chunkX = pagePos.x * Grid::IDXPAGE_SIZE + lx;
                int chunkY = pagePos.y * Grid::IDXPAGE_SIZE + ly;
                result.push_back({chunkX, chunkY});
            }
    }

    return result;
}

// ==== RASTER DATA ====
void RasterData::setPixel(int worldX, int worldY, RGBA color)
{
    if(color.a == 0) { erasePixel(worldX, worldY); return; }

    int chunkX = Grid::worldToChunk(worldX);
    int chunkY = Grid::worldToChunk(worldY);
    int clX = Grid::worldToChunkLocal(worldX);
    int clY = Grid::worldToChunkLocal(worldY);

    Chunk& accessedChunk = accessChunk(chunkX, chunkY);

    RGBA& px = accessedChunk.pixel(clX, clY);
    if(px.a == 0) accessedChunk.m_pixelCount++; // update occupancy
    px = color;

    // bounds
    accessedChunk.localBounds.expand(clX, clY); // local (chunk)
    m_pixelBounds.expand(worldX, worldY); // global (raster)
}
void RasterData::erasePixel(int worldX, int worldY)
{
    int chunkX = Grid::worldToChunk(worldX);
    int chunkY = Grid::worldToChunk(worldY);
    int clX = Grid::worldToChunkLocal(worldX);
    int clY = Grid::worldToChunkLocal(worldY);

    Chunk* chunk = readChunk(chunkX, chunkY);
    if(!chunk) return;

    if(chunk->pixel(clX, clY).a == 0) return; // already transparent

    chunk->pixel(clX, clY) = {0, 0, 0, 0};
    chunk->m_pixelCount--;

    markPixelErased(*chunk, chunkX, chunkY, clX, clY);

    // if(chunk->isEmpty()) freeChunk(chunkX, chunkY); // self-cleaning
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

    for(Key::XY pos : m_grid.listOccupiedChunks())
    {
        Chunk* chunk = m_grid.readChunk(pos.x, pos.y);
        if(!chunk) continue;

        const BoundsI& local = chunk->getLocalBounds();
        if(!local.valid) continue;

        // convert coords
        int worldMinX = Grid::chunkToWorld(pos.x, local.minX);
        int worldMaxX = Grid::chunkToWorld(pos.x, local.maxX);
        int worldMinY = Grid::chunkToWorld(pos.y, local.minY);
        int worldMaxY = Grid::chunkToWorld(pos.y, local.maxY);

        m_pixelBounds.expand(worldMinX, worldMinY);
        m_pixelBounds.expand(worldMaxX, worldMaxY);
    }

    m_pixelBounds.dirty = false; // cleared
}