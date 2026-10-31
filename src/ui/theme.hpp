#pragma once

struct ImFont;

// The UI font's size at scale 1, points: what a UI unit (Widgets::metrics().unit) is measured against.
constexpr float kBaseFontSize = 15.0f;

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
