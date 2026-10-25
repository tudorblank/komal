#pragma once

// Fonts and ImGui style.
// loadUiFonts():  once, after ImGui::CreateContext().
// applyUiStyle(): at startup and whenever the window's content scale changes; it rebuilds
//                 the style from scratch, so repeated calls don't compound.
void loadUiFonts();
void applyUiStyle(float contentScale);
