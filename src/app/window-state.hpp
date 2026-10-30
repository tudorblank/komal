#pragma once
// The main window's size between runs, in window.cfg in the settings folder: saved on exit,
// opened with on the next start.

#include <string>

struct WindowState {
    int  width = 0, height = 0;   // when not maximized
    bool maximized = false;

    static WindowState load(const std::string& path);   // {} if there's none
    void save(const std::string& path) const;
};
