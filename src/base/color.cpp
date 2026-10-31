#include "base/color.hpp"

#include <cstdio>

std::string Color8::text() const
{
    char buf[10];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", r, g, b, a);
    return buf;
}

std::optional<Color8> Color8::parse(std::string_view text)
{
    if (text.size() != 9 || text[0] != '#') return std::nullopt;
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    uint8_t bytes[4];
    for (size_t i = 0; i < 4; ++i) {
        const int hi = hex(text[1 + 2 * i]), lo = hex(text[2 + 2 * i]);
        if (hi < 0 || lo < 0) return std::nullopt;
        bytes[i] = uint8_t(hi * 16 + lo);
    }
    return Color8{ bytes[0], bytes[1], bytes[2], bytes[3] };
}
