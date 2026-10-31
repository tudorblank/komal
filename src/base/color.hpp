#pragma once
// Colours as values: Color8, straight (not premultiplied) 8-bit RGBA, as a picker or the look
// gives it; and the one way a 0..1 value becomes a byte. A picture's pixels are premultiplied:
// that's raster's RGBA, made from a Color8 by premultiply() (raster/tile.hpp).

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// 0..1 -> 0..255, rounded (out of range: clamped).
constexpr uint8_t unitToByte(float v) { return uint8_t(std::clamp(int(v * 255.0f + 0.5f), 0, 255)); }

struct Color8 {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    bool operator==(const Color8&) const = default;
    // ImGui's ImU32 (IM_COL32's byte order).
    constexpr uint32_t packed() const { return uint32_t(a) << 24 | uint32_t(b) << 16 | uint32_t(g) << 8 | uint32_t(r); }
    constexpr Color8 withAlpha(uint8_t alpha) const { return { r, g, b, alpha }; }
    constexpr std::array<float, 4> floats() const { return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f }; }
    // From 0..1 channels (a picker's), straight.
    static constexpr Color8 fromUnit(const float (&rgba)[4]) { return { unitToByte(rgba[0]), unitToByte(rgba[1]), unitToByte(rgba[2]), unitToByte(rgba[3]) }; }

    // As text: "#RRGGBBAA" (upper-case hex). parse() takes that, lower case too; else empty.
    std::string text() const;
    static std::optional<Color8> parse(std::string_view text);
};

constexpr Color8 grey8(uint8_t v, uint8_t a = 255) { return { v, v, v, a }; }

// A packed colour (ImU32) with its alpha replaced.
constexpr uint32_t withAlpha(uint32_t packed, uint8_t alpha) { return (packed & 0x00FFFFFFu) | uint32_t(alpha) << 24; }
