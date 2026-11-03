#include "node/node.hpp"
#include "ui/style/graph-style.hpp"
#include "ui/style/palette.hpp"
#include "ui/style/theme.hpp"
#include "ui/style/ui-style.hpp"

#include <doctest/doctest.h>
#include <string>

namespace {

// A theme file with every key, each colour `fill` unless a line below replaces it.
std::string themeText(const std::string& ramp, const std::string& hues)
{
    return "name = \"Test\"\nramp = [" + ramp + "]\nshadow = \"#000000\"\n[hues]\n" + hues;
}

std::string ramp(size_t steps, const char* colour = "\"#101010\"")
{
    std::string r;
    for (size_t i = 0; i < steps; ++i) r += (i ? ", " : "") + std::string(colour);
    return r;
}

const char* kHues = "accent = \"#8CB4F5\"\naccentDeep = \"#6888BA\"\nred = \"#D64848\"\nrose = \"#EC7070\"\n"
                    "green = \"#7ACE7A\"\nblue = \"#7698F0\"\nmaroon = \"#642424\"\norange = \"#FFA028\"\n"
                    "yellow = \"#C7C729\"\nwine = \"#83314A\"\nocean = \"#246283\"\nviolet = \"#5C3A8C\"\nteal = \"#1D725E\"\n";

std::optional<Theme> parse(const std::string& text, std::string& why) { return Theme::parse(text, why); }

}

TEST_CASE("every built-in theme reads")
{
    REQUIRE(Theme::names().size() >= 2);
    for (std::string_view name : Theme::names()) {
        std::string why;
        const std::optional<Theme> t = Theme::load(name, why);
        CHECK_MESSAGE(t, name, ": ", why);
    }
}

TEST_CASE("a complete theme file reads, colours as written")
{
    std::string why;
    const std::optional<Theme> t = parse(themeText(ramp(Theme::kSteps), kHues), why);
    REQUIRE_MESSAGE(t, why);
    CHECK(t->name == "Test");
    CHECK(t->ramp[0] == Color8{ 0x10, 0x10, 0x10, 255 });
    CHECK(t->accent == Color8{ 0x8C, 0xB4, 0xF5, 255 });
    CHECK(t->teal == Color8{ 0x1D, 0x72, 0x5E, 255 });
}

TEST_CASE("a broken theme file is refused, saying why")
{
    std::string why;
    SUBCASE("not TOML") {
        CHECK_FALSE(parse("name = \nramp", why));
        CHECK(why.find("line") != std::string::npos);
    }
    SUBCASE("no name") {
        CHECK_FALSE(parse("ramp = [" + ramp(Theme::kSteps) + "]\nshadow = \"#000000\"\n[hues]\n" + kHues, why));
        CHECK(why == "no name");
    }
    SUBCASE("a ramp too short or too long") {
        CHECK_FALSE(parse(themeText(ramp(Theme::kSteps - 1), kHues), why));
        CHECK_FALSE(parse(themeText(ramp(Theme::kSteps + 1), kHues), why));
        CHECK(why.find("ramp needs") != std::string::npos);
    }
    SUBCASE("a step that isn't #RRGGBB") {
        for (const char* bad : { "\"101010\"", "\"#1010\"", "\"#10101010\"", "\"#GG1010\"", "16" }) {
            CHECK_FALSE(parse(themeText(ramp(Theme::kSteps, bad), kHues), why));
            CHECK(why.find("ramp step 0") != std::string::npos);
        }
    }
    SUBCASE("a hue missing") {
        std::string hues = kHues;
        hues.erase(hues.find("teal"));
        CHECK_FALSE(parse(themeText(ramp(Theme::kSteps), hues), why));
        CHECK(why.find("teal") != std::string::npos);
    }
    SUBCASE("no [hues]") {
        CHECK_FALSE(parse("name = \"T\"\nramp = [" + ramp(Theme::kSteps) + "]\nshadow = \"#000000\"\n", why));
        CHECK(why == "no [hues]");
    }
}

TEST_CASE("an unknown theme is refused and the one in use stays")
{
    std::string why;
    REQUIRE(Style::useTheme("main-dark", why));
    const uint64_t revision = Palette::revision();
    const Color8 panel = Palette::current().panel;
    CHECK_FALSE(Style::useTheme("no-such-theme", why));
    CHECK(why.find("no-such-theme") != std::string::npos);
    CHECK(Palette::revision() == revision);
    CHECK(Palette::current().panel == panel);
}

TEST_CASE("switching themes: the palette and everything made from it follow")
{
    for (std::string_view name : Theme::names()) {
        std::string why;
        REQUIRE(Style::useTheme(name, why));
        const Theme t = *Theme::load(name, why);
        const Palette& p = Palette::current();
        CHECK(p.panel == t.ramp[4]);
        CHECK(p.accent == t.accent);
        CHECK(Style::canvas().background == p.canvasBackground);
        CHECK(Style::canvas().selection == p.accent);
        CHECK(GraphStyle::colors().canvas == p.graphCanvas.packed());
        CHECK(GraphStyle::kindHeader(NodeType::Raster) == t.wine.packed());
        CHECK(GraphStyle::kindHeader(NodeType::Reference) == t.ocean.packed());
        CHECK(GraphStyle::kindHeader(NodeType::Move) == t.violet.packed());
        CHECK(GraphStyle::kindHeader(NodeType::Compositor) == t.teal.packed());
    }
}

TEST_CASE("main-dark keeps its look")
{
    std::string why;
    REQUIRE(Style::useTheme("main-dark", why));
    const Palette& p = Palette::current();
    CHECK(p.panel == grey8(22));
    CHECK(p.windowBackground == grey8(14));
    CHECK(p.text == grey8(219));
    CHECK(p.accent == Color8{ 140, 180, 245, 255 });
    CHECK(p.border == grey8(43, 204));
}
