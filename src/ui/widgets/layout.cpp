#include "ui/widgets/interaction.hpp"

using namespace Widgets::detail;

namespace Widgets {

// ---------------------------------------------------------------- panels

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

void sectionGap()
{
    ImGui::Dummy(ImVec2(0.0f, Style::metrics().sectionGap));
}

void heading(const char* text)
{
    const Style::HeadingStyle& s = Style::get().heading;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 t = ImGui::CalcTextSize(text);
    const float right = p.x + ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(t.x + 2.0f * s.padding.x, s.height()));
    Skin::heading(Painter::window(), s, p, ImVec2(right, p.y + s.height()), text);
}

float stackHeight(int rows, int headings, int gaps)
{
    const Style::Styles& s = Style::get();
    const float sy = ImGui::GetStyle().ItemSpacing.y;
    // Each item is followed by the item spacing; the last one's is dropped (the panel's padding follows).
    const int items = rows + headings + gaps;
    return float(rows) * s.metrics.rowHeight + float(headings) * s.heading.height() + float(gaps) * s.metrics.sectionGap +
           float(std::max(0, items - 1)) * sy;
}

// ---------------------------------------------------------------- layout

void rowLabel(const char* text, float width)
{
    const float x = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(x + (width > 0.0f ? width : Style::metrics().labelWidth));
}

void sameRow()
{
    ImGui::SameLine(0.0f, Style::metrics().spacing);
}

void nextGroup()
{
    ImGui::SameLine(0.0f, Style::metrics().groupSpacing);
}

float stretchWidth(float reservedRight)
{
    return std::max(8.0f, ImGui::GetContentRegionAvail().x - (reservedRight > 0.0f ? reservedRight + Style::metrics().spacing : 0.0f));
}

void alignRight(float width)
{
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
}

}
