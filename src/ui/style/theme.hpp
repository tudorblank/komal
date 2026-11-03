#pragma once
// A theme: the colours the look is made of, read from a theme file (assets/themes/*.toml, baked
// into the binary). Only the palette reads one (palette.cpp), giving each colour its jobs: the
// file is where a colour's value lives, the palette is where its use does.
//
//   std::string why;
//   std::optional<Theme> t = Theme::load("main-dark", why);

#include "base/color.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct Theme {
    static constexpr size_t kSteps = 24;

    std::string name;                     // as the file says
    std::array<Color8, kSteps> ramp{};   // the surfaces, lines and text: slots, any colour
    Color8 shadow;                        // under and behind things
    Color8 accent, accentDeep;            // the hues: slots by family
    Color8 red, rose, green, blue, maroon, orange, yellow;

    // Colour maths, not a theme's: the colour picker's corners, a marker's inner ring.
    static constexpr Color8 kPureWhite = grey8(255);
    static constexpr Color8 kPureBlack = grey8(0);

    static const std::vector<std::string_view>& names();   // the built-in themes, by file name
    static std::optional<Theme> load(std::string_view name, std::string& why);   // a built-in one
    // From a theme file's text; every key must be there, each colour "#RRGGBB".
    static std::optional<Theme> parse(std::string_view text, std::string& why);
};
