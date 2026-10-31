#include "app/window-state.hpp"
#include "base/file.hpp"
#include "base/log.hpp"

#include <cstdio>
#include <string>
#include <vector>

WindowState WindowState::load(const std::string& path)
{
    std::vector<uint8_t> bytes;
    std::string error;
    if (!File::read(path, bytes, error)) return {};   // none yet
    const std::string text(bytes.begin(), bytes.end());
    WindowState s;
    int m = 0;
    if (std::sscanf(text.c_str(), "%d %d %d", &s.width, &s.height, &m) != 3) return {};
    s.maximized = m != 0;
    return s;
}

void WindowState::save(const std::string& path) const
{
    char text[64];
    std::snprintf(text, sizeof text, "%d %d %d\n", width, height, maximized ? 1 : 0);
    std::string error;
    if (!File::write(path, std::string(text), error)) Log::error("settings", "%s: %s", path.c_str(), error.c_str());
}
