#include "ui/style/theme-list.hpp"
#include "base/file.hpp"
#include "base/log.hpp"

// Private to this file: the folder, and what refresh() found.
namespace {

std::string g_folder;
std::vector<ThemeList::Entry> g_all;   // empty until it's first listed

bool isDefaultId(std::string_view id)
{
    for (const Theme::Default& d : Theme::defaults())
        if (d.id == id) return true;
    return false;
}

void listDefaults()
{
    g_all.clear();
    for (const Theme::Default& d : Theme::defaults()) g_all.push_back({ std::string(d.id), {} });
}

}

namespace ThemeList {

void setFolder(std::string folder) { g_folder = std::move(folder); }

void refresh()
{
    listDefaults();
    if (g_folder.empty()) return;
    for (std::string& path : File::list(g_folder, ".toml")) {
        std::string id = File::stem(path);
        if (isDefaultId(id)) {
            Log::error("theme", "%s: Komal's own theme is called %s: skipped", path.c_str(), id.c_str());
            continue;
        }
        g_all.push_back({ std::move(id), std::move(path) });
    }
}

const std::vector<Entry>& all()
{
    if (g_all.empty()) listDefaults();
    return g_all;
}

std::optional<Theme> read(std::string_view id)
{
    for (const Theme::Default& d : Theme::defaults())
        if (d.id == id) return d.theme;

    for (const Entry& e : all()) {
        if (e.id != id) continue;
        std::vector<uint8_t> bytes;
        std::string error;
        if (!File::read(e.path, bytes, error)) {
            Log::error("theme", "%s: %s", e.path.c_str(), error.c_str());
            return std::nullopt;
        }
        std::optional<Theme> theme = Theme::fromToml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        if (!theme) Log::error("theme", "%s isn't a theme Komal can use", e.path.c_str());
        return theme;
    }

    Log::error("theme", "no theme called %.*s", int(id.size()), id.data());
    return std::nullopt;
}

}
