#include "ui/style/theme.hpp"
#include "base/log.hpp"

#include <toml++/toml.hpp>

// Private to this file: Komal's own themes, and reading a color from a .toml file.
namespace {

// "#RRGGBB" in a .toml file, opaque
std::optional<Color8> tomlColor(const toml::node* node)
{
    const auto text = node ? node->value<std::string_view>() : std::nullopt;
    if (!text || text->size() != 7) return std::nullopt;
    return Color8::parse(std::string(*text) + "FF");
}

// The ramp: 24 steps the surfaces, lines and text are made of. A step is a slot, not a grey: any
// color goes. In a dark theme they climb from dark to light; a light theme turns that round.
// Each comment is what the step is used for (palette.cpp says it in code). The hues are colors
// by family: a slot, like a terminal theme's "red".

// Komal's own look.
Theme mainDark()
{
    return {
        .name = "Main Dark",
        .ramp = {
            hex("#0A0A0A"),   //  0  rims: grabs, node borders, ports
            hex("#0E0E0E"),   //  1  the window, behind every panel
            hex("#111111"),   //  2  title and menu bars, tabs
            hex("#141414"),   //  3  the canvas and the graph's, a node's slider track
            hex("#161616"),   //  4  a panel
            hex("#1C1C1C"),   //  5  popups, notices, a node's insets
            hex("#202020"),   //  6  fields, table headers
            hex("#252525"),   //  7  buttons, a node's body, a selected row
            hex("#2B2B2B"),   //  8  borders, a field under the mouse
            hex("#303030"),   //  9  a button under the mouse, a node's rows, a slider's track
            hex("#383838"),   // 10  a button held, a scrollbar
            hex("#3E3E3E"),   // 11  a picture not loaded yet
            hex("#474747"),   // 12  a scrollbar under the mouse
            hex("#505050"),   // 13  a scrollbar held, a resize grip
            hex("#5E5E5E"),   // 14  a toggle's edge
            hex("#696969"),   // 15  a node's border under the mouse
            hex("#767676"),   // 16  dim text, icons off
            hex("#8C8C8C"),   // 17  a toggle under the mouse
            hex("#9B9B9B"),   // 18  a node's dim text, mask ports
            hex("#CDCDCD"),   // 19  a slider's grab
            hex("#DBDBDB"),   // 20  text
            hex("#E7E7E7"),   // 21  a node's title
            hex("#F0F0F0"),   // 22  icons on
            hex("#FFFFFF"),   // 23  the front: a grab under the mouse; faint lines and glows (with an alpha)
        },
        .shadow = hex("#000000"),   // under and behind things, with an alpha
        .accent     = hex("#8CB4F5"),   // selections, focus, checked things
        .accentDeep = hex("#6888BA"),   // a slider's filled part
        .red        = hex("#D64848"),   // outlines on maps, notices
        .rose       = hex("#EC7070"),   // the R channel
        .green      = hex("#7ACE7A"),   // the G channel
        .blue       = hex("#7698F0"),   // the B channel
        .maroon     = hex("#642424"),   // the master node's title bar
        .orange     = hex("#FFA028"),   // the raster the canvas paints into
        .yellow     = hex("#C7C729"),   // image ports and links
        .wine       = hex("#83314A"),   // a Raster node's title bar
        .ocean      = hex("#246283"),   // a Reference node's title bar
        .violet     = hex("#5C3A8C"),   // a Move node's title bar
        .teal       = hex("#1D725E"),   // a Compositor node's title bar
    };
}

// A first draft: light surfaces and dark text.
Theme mainLight()
{
    return {
        .name = "Main Light",
        .ramp = {
            hex("#969696"),   //  0  rims: grabs, node borders, ports
            hex("#C4C4C4"),   //  1  the window, behind every panel
            hex("#D6D6D6"),   //  2  title and menu bars, tabs
            hex("#E2E2E2"),   //  3  the canvas and the graph's, a node's slider track
            hex("#ECECEC"),   //  4  a panel
            hex("#F6F6F6"),   //  5  popups, notices, a node's insets
            hex("#FCFCFC"),   //  6  fields, table headers
            hex("#DEDEDE"),   //  7  buttons, a node's body, a selected row
            hex("#D2D2D2"),   //  8  borders, a field under the mouse
            hex("#C8C8C8"),   //  9  a button under the mouse, a node's rows, a slider's track
            hex("#BCBCBC"),   // 10  a button held, a scrollbar
            hex("#B0B0B0"),   // 11  a picture not loaded yet
            hex("#A6A6A6"),   // 12  a scrollbar under the mouse
            hex("#969696"),   // 13  a scrollbar held, a resize grip
            hex("#848484"),   // 14  a toggle's edge
            hex("#6E6E6E"),   // 15  a node's border under the mouse
            hex("#808080"),   // 16  dim text, icons off
            hex("#646464"),   // 17  a toggle under the mouse
            hex("#606060"),   // 18  a node's dim text, mask ports
            hex("#484848"),   // 19  a slider's grab
            hex("#222222"),   // 20  text
            hex("#141414"),   // 21  a node's title
            hex("#1C1C1C"),   // 22  icons on
            hex("#000000"),   // 23  the front: a grab under the mouse; faint lines and glows (with an alpha)
        },
        .shadow = hex("#000000"),   // under and behind things, with an alpha
        .accent     = hex("#4278D7"),   // selections, focus, checked things
        .accentDeep = hex("#78A0E1"),   // a slider's filled part
        .red        = hex("#C83232"),   // outlines on maps, notices
        .rose       = hex("#D25050"),   // the R channel
        .green      = hex("#3C963C"),   // the G channel
        .blue       = hex("#3C64D2"),   // the B channel
        .maroon     = hex("#DEACA6"),   // the master node's title bar
        .orange     = hex("#E68214"),   // the raster the canvas paints into
        .yellow     = hex("#AF9600"),   // image ports and links
        .wine       = hex("#D696AA"),   // a Raster node's title bar
        .ocean      = hex("#8CB9D7"),   // a Reference node's title bar
        .violet     = hex("#B4A0D7"),   // a Move node's title bar
        .teal       = hex("#82C3AF"),   // a Compositor node's title bar
    };
}

}

// ---- Komal's own

std::span<const Theme::Default> Theme::defaults()
{
    static const Default themes[] = {
        { "main-dark",  mainDark() },
        { "main-light", mainLight() },
    };
    return themes;
}

// ---- An extra one, from a .toml file's text

std::optional<Theme> Theme::fromToml(std::string_view text)
{
    toml::table file;
    try {
        file = toml::parse(text);
    } catch (const toml::parse_error& e) {
        Log::error("theme", "line %u: %.*s", unsigned(e.source().begin.line), int(e.description().size()), e.description().data());
        return std::nullopt;
    }

    Theme t;
    const auto name = file["name"].value<std::string>();
    if (!name) { Log::error("theme", "no name"); return std::nullopt; }
    t.name = *name;

    const toml::array* ramp = file["ramp"].as_array();
    if (!ramp || ramp->size() != kSteps) { Log::error("theme", "%s: the ramp needs %zu colors", t.name.c_str(), kSteps); return std::nullopt; }
    for (size_t i = 0; i < kSteps; ++i) {
        const auto c = tomlColor(ramp->get(i));
        if (!c) { Log::error("theme", "%s: ramp step %zu isn't a \"#RRGGBB\" color", t.name.c_str(), i); return std::nullopt; }
        t.ramp[i] = *c;
    }

    const auto read = [&](const toml::node* node, std::string_view key, Color8& out) {
        const auto c = tomlColor(node);
        if (!c) { Log::error("theme", "%s: %.*s is missing or isn't a \"#RRGGBB\" color", t.name.c_str(), int(key.size()), key.data()); return false; }
        out = *c;
        return true;
    };
    if (!read(file.get("shadow"), "shadow", t.shadow)) return std::nullopt;

    const toml::table* hues = file["hues"].as_table();
    if (!hues) { Log::error("theme", "%s: no [hues]", t.name.c_str()); return std::nullopt; }
    const std::pair<std::string_view, Color8*> slots[] = {
        { "accent", &t.accent }, { "accentDeep", &t.accentDeep }, { "red", &t.red }, { "rose", &t.rose },
        { "green", &t.green }, { "blue", &t.blue }, { "maroon", &t.maroon }, { "orange", &t.orange }, { "yellow", &t.yellow },
        { "wine", &t.wine }, { "ocean", &t.ocean }, { "violet", &t.violet }, { "teal", &t.teal } };
    for (const auto& [key, out] : slots)
        if (!read(hues->get(key), key, *out)) return std::nullopt;
    return t;
}