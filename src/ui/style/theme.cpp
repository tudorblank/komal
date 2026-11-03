#include "ui/style/theme.hpp"

#include <toml++/toml.hpp>

extern const unsigned char kThemeMainDark[];
extern const unsigned int  kThemeMainDarkSize;
extern const unsigned char kThemeMainLight[];
extern const unsigned int  kThemeMainLightSize;

namespace {

struct BuiltIn {
    std::string_view name;
    std::string_view text;
};

const std::vector<BuiltIn>& builtIns()
{
    static const std::vector<BuiltIn> themes{
        { "main-dark",  { reinterpret_cast<const char*>(kThemeMainDark),  kThemeMainDarkSize } },
        { "main-light", { reinterpret_cast<const char*>(kThemeMainLight), kThemeMainLightSize } },
    };
    return themes;
}

// "#RRGGBB", opaque
std::optional<Color8> colour(const toml::node* node)
{
    const auto text = node ? node->value<std::string_view>() : std::nullopt;
    if (!text || text->size() != 7) return std::nullopt;
    return Color8::parse(std::string(*text) + "FF");
}

}

const std::vector<std::string_view>& Theme::names()
{
    static const std::vector<std::string_view> list = [] {
        std::vector<std::string_view> n;
        for (const BuiltIn& b : builtIns()) n.push_back(b.name);
        return n;
    }();
    return list;
}

std::optional<Theme> Theme::load(std::string_view name, std::string& why)
{
    for (const BuiltIn& b : builtIns()) {
        if (b.name != name) continue;
        std::optional<Theme> t = parse(b.text, why);
        if (!t) why = std::string(name) + ": " + why;
        return t;
    }
    why = "no theme called " + std::string(name);
    return std::nullopt;
}

std::optional<Theme> Theme::parse(std::string_view text, std::string& why)
{
    toml::table file;
    try {
        file = toml::parse(text);
    } catch (const toml::parse_error& e) {
        why = "line " + std::to_string(e.source().begin.line) + ": " + std::string(e.description());
        return std::nullopt;
    }

    Theme t;
    const auto name = file["name"].value<std::string>();
    if (!name) { why = "no name"; return std::nullopt; }
    t.name = *name;

    const toml::array* ramp = file["ramp"].as_array();
    if (!ramp || ramp->size() != kSteps) { why = "the ramp needs " + std::to_string(kSteps) + " colours"; return std::nullopt; }
    for (size_t i = 0; i < kSteps; ++i) {
        const auto c = colour(ramp->get(i));
        if (!c) { why = "ramp step " + std::to_string(i) + " isn't a \"#RRGGBB\" colour"; return std::nullopt; }
        t.ramp[i] = *c;
    }

    const auto read = [&](const toml::node* node, std::string_view key, Color8& out) {
        const auto c = colour(node);
        if (!c) { why = std::string(key) + " is missing or isn't a \"#RRGGBB\" colour"; return false; }
        out = *c;
        return true;
    };
    if (!read(file.get("shadow"), "shadow", t.shadow)) return std::nullopt;

    const toml::table* hues = file["hues"].as_table();
    if (!hues) { why = "no [hues]"; return std::nullopt; }
    const std::pair<std::string_view, Color8*> slots[] = {
        { "accent", &t.accent }, { "accentDeep", &t.accentDeep }, { "red", &t.red }, { "rose", &t.rose },
        { "green", &t.green }, { "blue", &t.blue }, { "maroon", &t.maroon }, { "orange", &t.orange }, { "yellow", &t.yellow },
        { "wine", &t.wine }, { "ocean", &t.ocean }, { "violet", &t.violet }, { "teal", &t.teal } };
    for (const auto& [key, out] : slots)
        if (!read(hues->get(key), key, *out)) return std::nullopt;
    return t;
}
