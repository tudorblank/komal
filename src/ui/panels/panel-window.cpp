#include "ui/panels/panel-window.hpp"
#include "ui/style/ui-style.hpp"

#include <imgui_internal.h>   // BeginViewportSideBar

namespace Widgets {

PanelWindow::PanelWindow(const Panel& panel, ActionRegistry& actions, ImGuiWindowFlags flags, Padding padding)
{
    const float p = padding == Padding::Panel ? Style::metrics().panelPadding : 0.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(p, p));
    bool keep = true;
    m_shown = ImGui::Begin(panel.title(), &keep, flags);
    ImGui::PopStyleVar();
    // A docked panel's tab bar ends in a separator in the selected tab's color (the panel's):
    // the top padding counts from the tab bar, like every other side.
    if (m_shown && padding == Padding::Panel && ImGui::IsWindowDocked())
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().TabBarBorderSize);
    if (!keep) actions.run(panel.toggleAction());
}

PanelWindow::~PanelWindow()
{
    ImGui::End();
}

BarWindow::BarWindow(const Panel& panel, float height, ImVec2 padding, ImU32 back)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, back);
    m_shown = ImGui::BeginViewportSideBar(panel.title(), ImGui::GetMainViewport(), ImGuiDir_Up, height,
                                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

BarWindow::~BarWindow()
{
    ImGui::End();   // a side bar is a window: always ended
}

}
