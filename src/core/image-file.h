#pragma once
// Image files in: decoded to premultiplied RGBA, ready to blit into a raster.

#include "raster/tile.h"

#include <string>
#include <vector>

struct ImageFile {
    std::vector<RGBA> pixels;   // premultiplied, canonical, row by row
    int width = 0, height = 0;
    std::string name;           // file name without directory or extension
};

// Any format stb_image reads (PNG, JPEG, BMP, TGA, PSD composite, GIF first frame, ...).
// Safe to call from any thread.
bool loadImageFile(const std::string& path, ImageFile& out, std::string& error);
