#include "node/node.hpp"
#include "ui/style/graph-style.hpp"
#include "ui/style/palette.hpp"
#include "ui/style/theme.hpp"
#include "ui/style/theme-list.hpp"
#include "ui/style/ui-style.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

// A theme file with every key, each color `fill` unless a line below replaces it.
std::string themeText(const std::string& ramp, const std::string& hues)
{
    return "name = \"Test\"\nramp = [" + ramp + "]\nshadow = \"#000000\"\n[hues]\n" + hues;
}

std::string ramp(size_t steps, const char* color = "\"#101010\"")
{
    std::string r;
    for (size_t i = 0; i < steps; ++i) r += (i ? ", " : "") + std::string(color);
    return r;
}

const char* kHues = "accent = \"#8CB4F5\"\naccentDeep = \"#6888BA\"\nred = \"#D64848\"\nrose = \"#EC7070\"\n"
                    "green = \"#7ACE7A\"\nblue = \"#7698F0\"\nmaroon = \"#642424\"\norange = \"#FFA028\"\n"
                    "yellow = \"#C7C729\"\nwine = \"#83314A\"\nocean = \"#246283\"\nviolet = \"#5C3A8C\"\nteal = \"#1D725E\"\n";

}

TEST_CASE("Komal's own themes: listed first, always there")
{
    ThemeList::setFolder("");
    ThemeList::refresh();
    const std::vector<ThemeList::Entry>& all = ThemeList::all();
    REQUIRE(all.size() == 2);
    CHECK(all[0].id == "main-dark");
    CHECK(all[1].id == "main-light");
    for (const ThemeList::Entry& e : all) {
        CHECK(e.isDefault());
        CHECK_MESSAGE(ThemeList::read(e.id), e.id);
    }
}

TEST_CASE("extra themes: the folder's .toml files, found by refresh, read when used")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "komal-theme-tests";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto write = [&](const char* file, const std::string& text) { std::ofstream(dir / file) << text; };
    const std::string complete = themeText(ramp(Theme::kSteps), kHues);
    ThemeList::setFolder(dir.string() + "/");

    SUBCASE("one is listed after Komal's own, and reads") {
        write("sea.toml", complete);
        ThemeList::refresh();
        REQUIRE(ThemeList::all().size() == 3);
        CHECK(ThemeList::all()[2].id == "sea");
        CHECK_FALSE(ThemeList::all()[2].isDefault());
        const std::optional<Theme> t = ThemeList::read("sea");
        REQUIRE(t);
        CHECK(t->name == "Test");
    }
    SUBCASE("only refresh finds files added or removed") {
        ThemeList::refresh();
        write("sea.toml", complete);
        CHECK(ThemeList::all().size() == 2);
        ThemeList::refresh();
        CHECK(ThemeList::all().size() == 3);
        fs::remove(dir / "sea.toml");
        ThemeList::refresh();
        CHECK(ThemeList::all().size() == 2);
    }
    SUBCASE("an edited file shows the next time it's read") {
        write("sea.toml", complete);
        ThemeList::refresh();
        REQUIRE(ThemeList::read("sea"));
        std::string hues = kHues;
        hues.replace(hues.find("#8CB4F5"), 7, "#FF0000");
        write("sea.toml", themeText(ramp(Theme::kSteps), hues));
        CHECK(ThemeList::read("sea")->accent == Color8{ 255, 0, 0, 255 });
    }
    SUBCASE("a broken one is listed, but can't be used: the theme in use stays") {
        write("broken.toml", "name = \n");
        ThemeList::refresh();
        CHECK(ThemeList::all().size() == 3);
        CHECK_FALSE(ThemeList::read("broken"));
        REQUIRE(Style::useTheme("main-dark"));
        const uint64_t revision = Palette::revision();
        CHECK_FALSE(Style::useTheme("broken"));
        CHECK(Palette::revision() == revision);
    }
    SUBCASE("one named like Komal's own is skipped: those are never replaced") {
        std::string hues = kHues;
        hues.replace(hues.find("#8CB4F5"), 7, "#FF0000");
        write("main-dark.toml", themeText(ramp(Theme::kSteps), hues));
        ThemeList::refresh();
        CHECK(ThemeList::all().size() == 2);
        CHECK(ThemeList::read("main-dark")->accent == Color8{ 0x8C, 0xB4, 0xF5, 255 });
    }
    SUBCASE("only .toml files, in any case") {
        write("notes.txt", complete);
        write("LOUD.TOML", complete);
        ThemeList::refresh();
        REQUIRE(ThemeList::all().size() == 3);
        CHECK(ThemeList::all()[2].id == "LOUD");
    }
    SUBCASE("a folder that isn't there: Komal's own only") {
        ThemeList::setFolder((dir / "nowhere").string() + "/");
        ThemeList::refresh();
        CHECK(ThemeList::all().size() == 2);
    }

    ThemeList::setFolder("");
    ThemeList::refresh();
    fs::remove_all(dir);
}

TEST_CASE("a complete theme file reads, colors as written")
{
    const std::optional<Theme> t = Theme::fromToml(themeText(ramp(Theme::kSteps), kHues));
    REQUIRE(t);
    CHECK(t->name == "Test");
    CHECK(t->ramp[0] == Color8{ 0x10, 0x10, 0x10, 255 });
    CHECK(t->accent == Color8{ 0x8C, 0xB4, 0xF5, 255 });
    CHECK(t->teal == Color8{ 0x1D, 0x72, 0x5E, 255 });
}

TEST_CASE("a broken theme file is refused")
{
    SUBCASE("not TOML") {
        CHECK_FALSE(Theme::fromToml("name = \nramp"));
    }
    SUBCASE("no name") {
        CHECK_FALSE(Theme::fromToml("ramp = [" + ramp(Theme::kSteps) + "]\nshadow = \"#000000\"\n[hues]\n" + kHues));
    }
    SUBCASE("a ramp too short or too long") {
        CHECK_FALSE(Theme::fromToml(themeText(ramp(Theme::kSteps - 1), kHues)));
        CHECK_FALSE(Theme::fromToml(themeText(ramp(Theme::kSteps + 1), kHues)));
    }
    SUBCASE("a step that isn't #RRGGBB") {
        for (const char* bad : { "\"101010\"", "\"#1010\"", "\"#10101010\"", "\"#GG1010\"", "16" }) {
            CHECK_FALSE(Theme::fromToml(themeText(ramp(Theme::kSteps, bad), kHues)));
        }
    }
    SUBCASE("a hue missing") {
        std::string hues = kHues;
        hues.erase(hues.find("teal"));
        CHECK_FALSE(Theme::fromToml(themeText(ramp(Theme::kSteps), hues)));
    }
    SUBCASE("no [hues]") {
        CHECK_FALSE(Theme::fromToml("name = \"T\"\nramp = [" + ramp(Theme::kSteps) + "]\nshadow = \"#000000\"\n"));
    }
}

TEST_CASE("an unknown theme is refused and the one in use stays")
{
    REQUIRE(Style::useTheme("main-dark"));
    const uint64_t revision = Palette::revision();
    const Color8 panel = Palette::current().panel;
    CHECK_FALSE(Style::useTheme("no-such-theme"));
    CHECK(Palette::revision() == revision);
    CHECK(Palette::current().panel == panel);
}

TEST_CASE("switching themes: the palette and everything made from it follow")
{
    for (const ThemeList::Entry& e : ThemeList::all()) {
        REQUIRE(Style::useTheme(e.id));
        const Theme t = *ThemeList::read(e.id);
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
    REQUIRE(Style::useTheme("main-dark"));
    const Palette& p = Palette::current();
    CHECK(p.panel == grey8(22));
    CHECK(p.windowBackground == grey8(14));
    CHECK(p.text == grey8(219));
    CHECK(p.accent == Color8{ 140, 180, 245, 255 });
    CHECK(p.border == grey8(43, 204));
}
