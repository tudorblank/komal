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

    source.bakeTile(tile, tileX, tileY);

    return tile;
}

void CompositorNode::bakeTile(Chunk& out, int tileX, int tileY)
{
    int originX = tileX * Chunk::SIZE;
    int originY = tileY * Chunk::SIZE;

    for(int i = 0; i < Chunk::SIZE * Chunk::SIZE; i++)
        out.data[i] = transparent();

    for(auto& layer : m_layers)
    {
        if(!layer) continue;

        float opacity = layer->m_opacity;
        float fill = layer->m_fill;
        bool needsAlphaAdjust = (opacity < 1.0f) || (fill < 1.0f);

        if(Chunk* fastChunk = layer->tryGetTileChunk(tileX, tileY))
        {
            for(int ly = 0; ly < Chunk::SIZE; ly++)
            {
                for(int lx = 0; lx < Chunk::SIZE; lx++)
                {
                    RGBA c = fastChunk->pixel(lx, ly);
                    if(needsAlphaAdjust)
                    {
                        if(fill < 1.0f)    c.a = (uint8_t)(c.a * fill + 0.5f);
                        if(opacity < 1.0f) c.a = (uint8_t)(c.a * opacity + 0.5f);
                    }
                    out.pixel(lx, ly) = over(out.pixel(lx, ly), c);
                }
            }
        }
        else // fallback: no shortcut available, sample point-by-point
        {
            for(int ly = 0; ly < Chunk::SIZE; ly++)
                for(int lx = 0; lx < Chunk::SIZE; lx++)
                    out.pixel(lx, ly) = over(out.pixel(lx, ly), layer->sample(originX + lx, originY + ly));
        }
    }
}

RGBA NodeCache::sample(CompositorNode& source, int worldX, int worldY)
{
    int tileX = Grid::worldToChunk(worldX);
    int tileY = Grid::worldToChunk(worldY);
    Chunk& tile = getOrCreateTile(source, tileX, tileY);
    return tile.pixel(Grid::worldToLocal(worldX), Grid::worldToLocal(worldY));
}