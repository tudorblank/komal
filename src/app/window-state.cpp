#include "app/window-state.hpp"

#include <cstdio>

WindowState WindowState::load(const std::string& path)
{
    WindowState s;
    if (FILE* f = std::fopen(path.c_str(), "r")) {
        int m = 0;
        if (std::fscanf(f, "%d %d %d", &s.width, &s.height, &m) == 3) s.maximized = m != 0;
        else s = {};
        std::fclose(f);
    }
    return s;
}

void WindowState::save(const std::string& path) const
{
    if (FILE* f = std::fopen(path.c_str(), "w")) {
        std::fprintf(f, "%d %d %d\n", width, height, maximized ? 1 : 0);
        std::fclose(f);
    }
}
