#pragma once
// colors as values: Color8, straight (not premultiplied) 8-bit RGBA
// A picture's pixels are premultiplied:
// that's raster's RGBA, made from a Color8 by premultiply() (raster/tile.hpp).

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// 0..1 -> 0..255, rounded (out of range: clamped)
constexpr uint8_t unitToByte(float v) { return uint8_t(std::clamp(int(v * 255.0f + 0.5f), 0, 255)); }

struct Color8 {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    bool operator==(const Color8&) const = default;
    
    // ImGui's ImU32 (IM_COL32's byte order)
    constexpr uint32_t packed() const { return uint32_t(a) << 24 | uint32_t(b) << 16 | uint32_t(g) << 8 | uint32_t(r); }
    constexpr Color8 withAlpha(uint8_t alpha) const { return { r, g, b, alpha }; }
    constexpr std::array<float, 4> floats() const { return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f }; }
    
    // from 0..1 channels (a picker's), straight
    static constexpr Color8 fromUnit(const float (&rgba)[4]) { return { unitToByte(rgba[0]), unitToByte(rgba[1]), unitToByte(rgba[2]), unitToByte(rgba[3]) }; }

    // as text: "#RRGGBBAA" (upper-case hex)
    std::string text() const;
    static std::optional<Color8> parse(std::string_view text);
};

constexpr Color8 grey8(uint8_t v, uint8_t a = 255) { return { v, v, v, a }; }

// a hex digit's value, either case; -1 if it isn't one
constexpr int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// "#RRGGBB" as an opaque color, worked out while compiling: a typo is a build error
//   constexpr Color8 sky = hex("#8CB4F5");
consteval Color8 hex(const char (&text)[8])
{
    const auto digit = [](char c) {
        const int v = hexDigit(c);
        if (v < 0) throw "not a hex digit";
        return v;
    };
    if (text[0] != '#') throw "a color starts with #";
    const auto byte = [&](int at) { return uint8_t(digit(text[at]) * 16 + digit(text[at + 1])); };
    return { byte(1), byte(3), byte(5), 255 };
}

// true white and black, never themed: a picker's corners, a marker's inner ring
constexpr Color8 kPureWhite = grey8(255);
constexpr Color8 kPureBlack = grey8(0);
