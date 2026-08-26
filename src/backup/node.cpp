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
    Chunk& tile = getOrCreateTile(source, tileX, tileY);
    return tile.pixel(Grid::worldToLocal(worldX), Grid::worldToLocal(worldY));
}