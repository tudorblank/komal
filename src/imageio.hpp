#pragma once

#include "raster/raster-utils.hpp"
#include "raster/raster-base.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

void loadImageIntoRaster(const char* path, RasterData& target, int offsetX, int offsetY)
{
    int w, h, channels;
    unsigned char* data = stbi_load(path, &w, &h, &channels, 4); // force RGBA
    if(!data) { printf("Failed to load image: %s\n", path); return; }

    for(int y = 0; y < h; y++)
        for(int x = 0; x < w; x++)
        {
            unsigned char* p = &data[(y * w + x) * 4];
            RGBA color{ p[0], p[1], p[2], p[3] };
            target.setPixel(offsetX + x, offsetY + y, color);
        }

    stbi_image_free(data);
}
bool exportRasterToImage(RasterData& source, const char* path)
{
    BoundsI b = source.getPixelBounds();
    if(!b.valid) return false;

    int w = b.width();
    int h = b.height();
    std::vector<unsigned char> out((size_t)w * h * 4, 0);

    for(int cy = 0; cy < h; cy++)
        for(int cx = 0; cx < w; cx++)
        {
            int worldX = b.minX + cx;
            int worldY = b.minY + cy;

            int chunkX = Grid::worldToChunk(worldX);
            int chunkY = Grid::worldToChunk(worldY);
            Chunk* chunk = source.readChunk(chunkX, chunkY);

            RGBA c = chunk ? chunk->pixel(Grid::worldToChunkLocal(worldX), Grid::worldToChunkLocal(worldY))
                            : transparent();

            unsigned char* p = &out[((size_t)cy * w + cx) * 4];
            p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
        }

    return stbi_write_png(path, w, h, 4, out.data(), w * 4) != 0;
}