#include "node.hpp"

Chunk& NodeCache::getOrCreateTile(CompositorNode& source, int tileX, int tileY)
{
    uint64_t key = Key::pack(tileX, tileY);
    auto it = tileIndexMap.find(key);
    if(it != tileIndexMap.end())
        return m_chunkPool.getChunk(it->second);

    uint32_t idx = m_chunkPool.allocateIndex();
    tileIndexMap.emplace(key, idx);
    Chunk& tile = m_chunkPool.getChunk(idx);

    int originX = tileX * Chunk::SIZE;
    int originY = tileY * Chunk::SIZE;
    for(int ly = 0; ly < Chunk::SIZE; ly++)
        for(int lx = 0; lx < Chunk::SIZE; lx++)
            tile.pixel(lx, ly) = source.computeRaw(originX + lx, originY + ly);

    return tile;
}

RGBA NodeCache::sample(CompositorNode& source, int worldX, int worldY)
{
    int tileX = Grid::worldToChunk(worldX);
    int tileY = Grid::worldToChunk(worldY);
    uint64_t key = Key::pack(tileX, tileY);

    auto it = tileIndexMap.find(key);
    Chunk* tile;

    if(it != tileIndexMap.end()) // found
        tile = &m_chunkPool.getChunk(it->second); // reuse pool slot
    else // not found - pool slot from pool
    {
        // add
        uint32_t idx = m_chunkPool.allocateIndex();
        tileIndexMap.emplace(key, idx);
        // pull
        tile = &m_chunkPool.getChunk(idx);

        int originX = tileX * Chunk::SIZE;
        int originY = tileY * Chunk::SIZE;

        // add pixel to tile
        for(int ly = 0; ly < Chunk::SIZE; ly++)
            for(int lx = 0; lx < Chunk::SIZE; lx++)
                tile->pixel(lx, ly) = source.computeRaw(originX + lx, originY + ly);
    }

    int lx = Grid::worldToLocal(worldX);
    int ly = Grid::worldToLocal(worldY);
    return tile->pixel(lx, ly);
}