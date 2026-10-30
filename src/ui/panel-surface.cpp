#include "ui/panel-surface.hpp"

#include <imgui_internal.h>   // ImGuiWindow::RootWindow

void PanelSurface::show(const ImGuiWindow* window, ImVec2 origin, ImVec2 size)
{
    m_window = window;
    m_origin = origin;
    m_size   = size;
}

bool PanelSurface::contains(const ImGuiWindow* top, ImVec2 screen) const
{
    if (!m_window || !top || top->RootWindow != m_window) return false;
    return screen.x >= m_origin.x && screen.y >= m_origin.y && screen.x < m_origin.x + m_size.x && screen.y < m_origin.y + m_size.y;
}
