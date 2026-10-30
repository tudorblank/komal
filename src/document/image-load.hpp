#pragma once

#include "raster/tile.hpp"

#include <string>
#include <vector>

struct ImageFile {
    std::vector<RGBA> pixels;   // premultiplied, canonical, row by row
    int width = 0, height = 0;
    std::string name;           // file name without directory or extension
};

// stb_image reads (PNG, JPEG, BMP, TGA, PSD composite, GIF first frame, ...).
// multi-thread safe
bool loadImageFile(const std::string& path, ImageFile& out, std::string& error);
