#pragma once
// Every theme Komal can use, by id. Komal's own come first: written in C++ (Theme::defaults()),
// always there, never replaced. Then the extra ones: the .toml files in one folder (the app's
// themes/ beside the program), found by refresh() and read from disk each time one is used, so an
// edited file shows as soon as it's picked again.
//
//   ThemeList::setFolder(folder);   // once, at startup (the app)
//   ThemeList::refresh();           // looks in the folder again: added and removed files show
//   for (const ThemeList::Entry& e : ThemeList::all()) ...
//   std::optional<Theme> t = ThemeList::read("main-dark");

#include "ui/style/theme.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ThemeList {

struct Entry {
    std::string id;     // how it's asked for: "main-dark"; an extra one's file name without ".toml"
    std::string path;   // an extra one's file; empty for one of Komal's own
    bool isDefault() const { return path.empty(); }
};

void setFolder(std::string folder);   // where the extra themes are; refresh() looks there
// Lists the folder's .toml files again. One with the id of one of Komal's own is skipped (logged).
void refresh();
const std::vector<Entry>& all();      // Komal's own, then the extra ones by id
// Komal's own as written; an extra one read from its file now. Nothing if there's no such theme,
// or its file can't be read or isn't a theme (logged).
std::optional<Theme> read(std::string_view id);

}
