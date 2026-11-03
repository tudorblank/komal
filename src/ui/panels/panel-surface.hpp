#pragma once
// Where a panel draws its own content this frame (the canvas's picture, the graph): its window
// and screen rectangle. What lands on a panel (a dropped file) asks it.
//
//   m_surface.show(ImGui::GetCurrentWindowRead(), origin, size);   // drawn this frame
//   if (m_surface.contains(topWindow, point)) ...                    // it's on us

#include <imgui.h>

struct ImGuiWindow;

class PanelSurface {
public:
    void hide() { m_window = nullptr; }   // at the start of every draw: shown only once drawn
    void show(const ImGuiWindow* window, ImVec2 origin, ImVec2 size);
    bool shown() const { return m_window != nullptr; }

    ImVec2 origin() const { return m_origin; }
    ImVec2 size() const { return m_size; }
    // `screen` is on it, and `top` (the window in front there) is its own (or one inside it).
    bool contains(const ImGuiWindow* top, ImVec2 screen) const;
    ImVec2 local(ImVec2 screen) const { return ImVec2(screen.x - m_origin.x, screen.y - m_origin.y); }

private:
    const ImGuiWindow* m_window = nullptr;
    ImVec2 m_origin{}, m_size{};
};
