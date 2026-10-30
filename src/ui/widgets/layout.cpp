#include "ui/widgets/detail.hpp"

using namespace Widgets::detail;

namespace Widgets {

const Metrics& metrics()
{
    // Worked out again only when what it comes from changes: the font size and the style.
    static Metrics m;
    static float madeFor[3] = { -1.0f, -1.0f, -1.0f };
    const float font = ImGui::GetFontSize(), frame = ImGui::GetFrameHeight(), spacing = ImGui::GetStyle().ItemSpacing.x;
    if (font == madeFor[0] && frame == madeFor[1] && spacing == madeFor[2]) return m;
    madeFor[0] = font;
    madeFor[1] = frame;
    madeFor[2] = spacing;
    const float u = font / 15.0f;
    m.unit            = u;
    m.rowHeight       = frame;
    m.boxHeight       = std::round(font + 6.0f * u);
    m.fieldWidth      = std::round(font * 3.6f);
    m.labelWidth      = std::round(font * 5.0f);
    m.shortLabelWidth = std::round(font * 1.1f);
    m.spacing         = spacing;
    m.groupSpacing    = std::round(font);
    m.rounding        = std::round(3.0f * u);
    m.panelPadding    = std::round(10.0f * u);
    m.sectionGap      = std::round(6.0f * u);
    return m;
}

// ---------------------------------------------------------------- panels

PanelWindow::PanelWindow(const Panel& panel, ActionRegistry& actions, ImGuiWindowFlags flags, Padding padding)
{
    const float p = padding == Padding::Panel ? metrics().panelPadding : 0.0f;
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
    ImGui::Dummy(ImVec2(0.0f, metrics().sectionGap));
}

float stackHeight(int rows, int headings, int gaps)
{
    const Metrics m = metrics();
    const float sy = ImGui::GetStyle().ItemSpacing.y;
    // Each item is followed by the item spacing; the last one's is dropped (the panel's padding follows).
    const int items = rows + headings + gaps;
    return rows * m.rowHeight + headings * ImGui::GetTextLineHeight() + gaps * m.sectionGap + std::max(0, items - 1) * sy;
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
    ImGui::SameLine(x + (width > 0.0f ? width : metrics().labelWidth));
}

void sameRow()
{
    ImGui::SameLine(0.0f, metrics().spacing);
}

void nextGroup()
{
    ImGui::SameLine(0.0f, metrics().groupSpacing);
}

float stretchWidth(float reservedRight)
{
    return std::max(8.0f, ImGui::GetContentRegionAvail().x - (reservedRight > 0.0f ? reservedRight + metrics().spacing : 0.0f));
}

void alignRight(float width)
{
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
}

}
