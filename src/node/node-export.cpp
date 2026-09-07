#include "node-export.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>

bool exportMasterToPath(CompositorNode& master, const char* path)
{
    BoundsI b = master.computeBounds();
    if(!b.valid) return false;

    int w = b.width();
    int h = b.height();
    std::vector<unsigned char> out((size_t)w * h * 4, 0);
    RGBA* dst = reinterpret_cast<RGBA*>(out.data());

    int chunkStartX = Grid::worldToChunk(b.minX);
    int chunkStartY = Grid::worldToChunk(b.minY);
    int chunkEndX   = Grid::worldToChunk(b.maxX);
    int chunkEndY   = Grid::worldToChunk(b.maxY);

    for(int cy = chunkStartY; cy <= chunkEndY; cy++)
    for(int cx = chunkStartX; cx <= chunkEndX; cx++)
    {
        const Chunk& chunk = master.getCachedTile(cx, cy);

        int wxBase = Grid::chunkToWorld(cx, 0);
        int wyBase = Grid::chunkToWorld(cy, 0);

        int lxStart = std::max(0, b.minX - wxBase);
        int lyStart = std::max(0, b.minY - wyBase);
        int lxEnd   = std::min(Grid::CHUNK_SIZE, b.maxX + 1 - wxBase);
        int lyEnd   = std::min(Grid::CHUNK_SIZE, b.maxY + 1 - wyBase);

        for(int ly = lyStart; ly < lyEnd; ly++)
        {
            int dstY = (wyBase + ly) - b.minY;
            RGBA* dstRow = &dst[(size_t)dstY * w + (wxBase + lxStart - b.minX)];
            for(int lx = lxStart; lx < lxEnd; lx++, dstRow++)
                *dstRow = chunk.pixel(lx, ly);
        }
    }

    return stbi_write_png(path, w, h, 4, out.data(), w * 4) != 0;
}