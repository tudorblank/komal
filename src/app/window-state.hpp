#pragma once
// main window's size between runs, in window.cfg

#include <string>

struct WindowState {
    int  width = 0, height = 0;   // when not maximized
    bool maximized = false;

    static WindowState load(const std::string& path);   // {} if there's none
    void save(const std::string& path) const;
};
