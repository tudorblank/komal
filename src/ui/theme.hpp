#pragma once

struct ImFont;

void    loadUiFonts(); // once, after ImGui::CreateContext()
void    applyUiStyle(float contentScale); // at startup and whenever the window's content scale changes
ImFont* numberFont();

ImFont* graphFont();
ImFont* graphNumberFont();

struct NumberFont {
    NumberFont();   // pushes numberFont() at the current size
    ~NumberFont();  // pops it
    NumberFont(const NumberFont&) = delete;
    NumberFont& operator=(const NumberFont&) = delete;
};
