#include "node-compositor.hpp"

Chunk& NodeCache::getOrCreateTile(CompositorNode& inputCompositor, int tileX, int tileY)
{
    uint64_t key = Key::pack(tileX, tileY);
    auto it = tileIndexMap.find(key);
    if(it != tileIndexMap.end())
        return m_tilePool.getChunk(it->second);

    uint32_t idx = m_tilePool.allocateIndex();
    tileIndexMap.emplace(key, idx);
    Chunk& tile = m_tilePool.getChunk(idx);

    inputCompositor.buildTile(tile, tileX, tileY);

    return tile;
}
RGBA NodeCache::lookupPixel(CompositorNode& inputCompositor, int worldX, int worldY)
{
    int tileX = Grid::worldToChunk(worldX);
    int tileY = Grid::worldToChunk(worldY);
    Chunk& tile = getOrCreateTile(inputCompositor, tileX, tileY);
    return tile.pixel(Grid::worldToLocal(worldX), Grid::worldToLocal(worldY));
}

void CompositorNode::buildTile(Chunk& out, int tileX, int tileY)
{
    int originX = tileX * Chunk::SIZE;
    int originY = tileY * Chunk::SIZE;

    // skip tiles that don't overlap any layer content at all
    BoundsI contentBounds = computeBounds();
    bool overlapsContent = contentBounds.valid
        && originX + Chunk::SIZE > contentBounds.minX && originX <= contentBounds.maxX
        && originY + Chunk::SIZE > contentBounds.minY && originY <= contentBounds.maxY;
    if(!overlapsContent)
    {
        for(int i = 0; i < Chunk::SIZE * Chunk::SIZE; i++)
            out.data[i] = transparent();
        return;
    }

    for(int i = 0; i < Chunk::SIZE * Chunk::SIZE; i++)
        out.data[i] = transparent();

    for(auto& layer : m_layers)
    {
        if(!layer) continue;

        // skip layers whose own bounds don't reach this tile
        BoundsI layerBounds = layer->computeBounds();
        bool layerOverlapsTile = layerBounds.valid
            && originX + Chunk::SIZE > layerBounds.minX && originX <= layerBounds.maxX
            && originY + Chunk::SIZE > layerBounds.minY && originY <= layerBounds.maxY;
        if(!layerOverlapsTile) continue;

        float opacity = layer->m_opacity;
        float fill = layer->m_fill;
        bool needsAlphaAdjust = (opacity < 1.0f) || (fill < 1.0f);

        Chunk* fastChunk = layer->readSourceChunk(tileX, tileY);
        if(fastChunk != nullptr)
        {
            for(int ly = 0; ly < Chunk::SIZE; ly++)
                for(int lx = 0; lx < Chunk::SIZE; lx++)
                {
                    RGBA c = fastChunk->pixel(lx, ly);
                    if(needsAlphaAdjust)
                        c = applyOpacityFill(c, fill, opacity);
                    out.pixel(lx, ly) = over(out.pixel(lx, ly), c);
                }
        }
        else
        {
            for(int ly = 0; ly < Chunk::SIZE; ly++)
                for(int lx = 0; lx < Chunk::SIZE; lx++)
                    out.pixel(lx, ly) = over(out.pixel(lx, ly), layer->sampleBlended(originX + lx, originY + ly));
        }
    }
}