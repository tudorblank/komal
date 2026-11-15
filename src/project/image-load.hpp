#pragma once
// Image files decoded (stb_image: PNG, JPEG, BMP, TGA, PSD composite, GIF first frame, ...), for
// import. Safe on any thread. Refused before decoding when the picture is bigger than the app takes.

#include "raster/raster-data.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

constexpr uint64_t kMaxImagePixels = uint64_t(1) << 28;   // 16384 x 16384: a gigabyte decoded

// An image file decoded: RGBA8, premultiplied, row by row, in the decoder's own memory (held
// until it goes: the tiles take it from there, no copy in between).
class DecodedImage {
public:
    int width() const { return m_width; }
    int height() const { return m_height; }
    FlatImage flat() const;
    void release();

private:
    friend bool loadImageBytes(std::span<const uint8_t> file, DecodedImage& out, std::string& error);
    struct Free { void operator()(uint8_t* pixels) const; };
    std::unique_ptr<uint8_t, Free> m_pixels;
    int m_width = 0, m_height = 0;
};

bool loadImageFile(const std::string& path, DecodedImage& out, std::string& error);
bool loadImageBytes(std::span<const uint8_t> file, DecodedImage& out, std::string& error);   // a file's bytes, in memory
