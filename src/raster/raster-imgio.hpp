#pragma once

#include "raster/raster-utils.hpp"
#include "raster/raster-base.hpp"

void loadImageIntoRaster(const char* path, RasterData& target, int offsetX, int offsetY);
bool exportRasterToImage(RasterData& source, const char* path);