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
    // A docked panel's tab bar ends in a separator in the selected tab's colour (the panel's):
    // the top padding counts from the tab bar, like every other side.
    if (m_shown && padding == Padding::Panel && ImGui::IsWindowDocked())
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().TabBarBorderSize);
    if (!keep) actions.run(panel.toggleAction());
}

PanelWindow::~PanelWindow()
{
    ImGui::End();
}

void sectionGap()
{
    ImGui::Dummy(ImVec2(0.0f, Style::metrics().sectionGap));
}

float stackHeight(int rows, int headings, int gaps)
{
    const Style::Metrics& m = Style::metrics();
    const float sy = ImGui::GetStyle().ItemSpacing.y;
    // Each item is followed by the item spacing; the last one's is dropped (the panel's padding follows).
    const int items = rows + headings + gaps;
    return float(rows) * m.rowHeight + float(headings) * ImGui::GetTextLineHeight() + float(gaps) * m.sectionGap + float(std::max(0, items - 1)) * sy;
}

// ---------------------------------------------------------------- layout

void heading(const char* text)
{
    ImGui::TextUnformatted(text);
}

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
