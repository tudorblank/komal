#include "document/image-load.hpp"
#include "base/file.hpp"

#include <climits>
#include <span>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include <stb_image.h>

void DecodedImage::Free::operator()(uint8_t* pixels) const { stbi_image_free(pixels); }

FlatImage DecodedImage::flat() const
{
    return { std::span<const uint8_t>(m_pixels.get(), size_t(m_width) * size_t(m_height) * 4), m_width, m_height };
}

void DecodedImage::release()
{
    m_pixels.reset();
    m_width = m_height = 0;
}

bool loadImageFile(const std::string& path, DecodedImage& out, std::string& error)
{
    std::vector<uint8_t> file;
    if (!File::read(path, file, error)) return false;
    return loadImageBytes(file, out, error);
}

bool loadImageBytes(std::span<const uint8_t> file, DecodedImage& out, std::string& error)
{
    if (file.size() > size_t(INT_MAX)) {
        error = "file too large";
        return false;
    }
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(file.data(), int(file.size()), &w, &h, &channels)) {
        error = stbi_failure_reason() ? stbi_failure_reason() : "not an image";
        return false;
    }
    if (w <= 0 || h <= 0 || uint64_t(w) * uint64_t(h) > kMaxImagePixels || !TileUtils::inWorld(w) || !TileUtils::inWorld(h)) {
        error = "image too large (" + std::to_string(w) + " x " + std::to_string(h) + ")";
        return false;
    }
    stbi_uc* data = stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &channels, 4);
    if (!data) {
        error = stbi_failure_reason() ? stbi_failure_reason() : "unknown error";
        return false;
    }
    // Straight -> premultiplied, in place.
    const size_t count = size_t(w) * size_t(h);
    for (stbi_uc* p = data; p != data + count * 4; p += 4) {
        if (p[3] == 255) continue;
        const RGBA pm = premultiply({ p[0], p[1], p[2], p[3] });
        p[0] = pm.r;
        p[1] = pm.g;
        p[2] = pm.b;
    }
    out.m_pixels.reset(data);
    out.m_width  = w;
    out.m_height = h;
    return true;
}
