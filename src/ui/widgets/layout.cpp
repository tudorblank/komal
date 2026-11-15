#include "ui/widgets/interaction.hpp"

using namespace Widgets::detail;

namespace Widgets {

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
