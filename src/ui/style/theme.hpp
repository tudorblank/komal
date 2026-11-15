#pragma once
// A theme: the colors the look is made of. Two kinds: Komal's own (defaults(), written in C++ in
// theme.cpp) and extra ones (fromToml(), from .toml files). Which themes there are is the theme
// list's (theme-list.hpp). Only the palette uses one (palette.cpp), giving each color its jobs:
// the theme is where a color's value lives, the palette is where its use does.
//
//   const Theme& dark = Theme::defaults()[0].theme;    // main-dark
//   std::optional<Theme> t = Theme::fromToml(text);   // none: what was wrong is logged

#include "base/color.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

struct Theme {
    static constexpr size_t kSteps = 24;

    std::string name;
    std::array<Color8, kSteps> ramp{};    // the surfaces, lines and text: slots, any color
    Color8 shadow;                        // under and behind things
    Color8 accent, accentDeep;            // the hues: slots by family
    Color8 red, rose, green, blue, maroon, orange, yellow;
    Color8 wine, ocean, violet, teal;

    // ---- Komal's own (main-dark, main-light): written in C++ in theme.cpp. Always there, never read
    // from a file, never failing: a typo in one is a build error.
    struct Default;
    static std::span<const Default> defaults();   // main-dark first: the one Komal starts with

    // ---- An extra one: from a .toml file's text, every key, each color "#RRGGBB". Nothing if it
    // can't, and logs what went wrong.
    static std::optional<Theme> fromToml(std::string_view text);
};

struct Theme::Default {
    std::string_view id;   // how it's asked for: "main-dark"
    Theme theme;
};