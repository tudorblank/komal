#include "image-load.hpp"

#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include <stb_image.h>

bool loadImageFile(const std::string& path, ImageFile& out, std::string& error)
{
    int w = 0, h = 0, channels = 0;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        error = stbi_failure_reason() ? stbi_failure_reason() : "unknown error";
        return false;
    }
    if (!TileUtils::inWorld(w) || !TileUtils::inWorld(h)) {
        stbi_image_free(data);
        error = "image too large";
        return false;
    }
    out.width  = w;
    out.height = h;
    out.name   = std::filesystem::path(path).stem().string();
    out.pixels.resize(size_t(w) * size_t(h));
    const stbi_uc* s = data;
    for (RGBA& p : out.pixels) {   // straight -> premultiplied
        const uint8_t a = s[3];
        p = a == 255 ? RGBA{ s[0], s[1], s[2], 255 } : RGBA{ mul255(s[0], a), mul255(s[1], a), mul255(s[2], a), a };
        s += 4;
    }
    stbi_image_free(data);
    return true;
}
