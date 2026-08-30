#include "node-compositor.hpp"

Chunk& NodeCache::getOrCreateTile(CompositorNode& inputCompositor, int tileX, int tileY)
{
    Chunk* existing = m_grid.readChunk(tileX, tileY);
    if(existing) return *existing; // cache hit

    Chunk& tile = m_grid.accessOrCreateChunk(tileX, tileY); // access / create new
    inputCompositor.buildTile(tile, tileX, tileY); // build (through compositor)
    return tile;
}
RGBA NodeCache::lookupPixel(CompositorNode& inputCompositor, int worldX, int worldY)
{
    int tileX = Grid::worldToChunk(worldX);
    int tileY = Grid::worldToChunk(worldY);
    
    Chunk& tile = getOrCreateTile(inputCompositor, tileX, tileY);
    return tile.pixel(Grid::worldToChunkLocal(worldX), Grid::worldToChunkLocal(worldY));
}

void CompositorNode::buildTile(Chunk& out, int tileX, int tileY)
{
    int originX = tileX * Grid::CHUNK_SIZE;
    int originY = tileY * Grid::CHUNK_SIZE;

    // skip tiles that don't overlap any layer content at all
    BoundsI contentBounds = computeBounds();
    bool overlapsContent = contentBounds.valid
        && originX + Grid::CHUNK_SIZE > contentBounds.minX && originX <= contentBounds.maxX
        && originY + Grid::CHUNK_SIZE > contentBounds.minY && originY <= contentBounds.maxY;
    if(!overlapsContent)
    {
        for(int i = 0; i < Grid::CHUNK_SIZE * Grid::CHUNK_SIZE; i++)
            out.data[i] = transparent();
        return;
    }

    for(int i = 0; i < Grid::CHUNK_SIZE * Grid::CHUNK_SIZE; i++)
        out.data[i] = transparent();

    for(auto& layer : m_layers)
    {
        if(!layer) continue;

        // skip layers whose own bounds don't reach this tile
        BoundsI layerBounds = layer->computeBounds();
        bool layerOverlapsTile = layerBounds.valid
            && originX + Grid::CHUNK_SIZE > layerBounds.minX && originX <= layerBounds.maxX
            && originY + Grid::CHUNK_SIZE > layerBounds.minY && originY <= layerBounds.maxY;
        if(!layerOverlapsTile) continue;

        float opacity = layer->m_opacity;
        float fill = layer->m_fill;
        bool needsAlphaAdjust = (opacity < 1.0f) || (fill < 1.0f);

        Chunk* fastChunk = layer->readSourceChunk(tileX, tileY);
        if(fastChunk != nullptr)
        {
            for(int ly = 0; ly < Grid::CHUNK_SIZE; ly++)
                for(int lx = 0; lx < Grid::CHUNK_SIZE; lx++)
                {
                    RGBA c = fastChunk->pixel(lx, ly);
                    if(needsAlphaAdjust)
                        c = applyOpacityFill(c, fill, opacity);
                    out.pixel(lx, ly) = over(out.pixel(lx, ly), c);
                }
        }
        else
        {
            for(int ly = 0; ly < Grid::CHUNK_SIZE; ly++)
                for(int lx = 0; lx < Grid::CHUNK_SIZE; lx++)
                    out.pixel(lx, ly) = over(out.pixel(lx, ly), layer->sampleBlended(originX + lx, originY + ly));
        }
    }
}