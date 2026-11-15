#include "ui/widgets/interaction.hpp"

using namespace Widgets::detail;

namespace Widgets {

bool button(const char* label, float width)
{
    const Style::Styles& s = Style::get();
    const char* end = ImGui::FindRenderedTextEnd(label);
    if (width <= 0.0f) width = std::round(ImGui::CalcTextSize(label, end).x + 2.0f * s.metrics.framePadding);
    bool clicked = false, hot = false;
    const ImRect box = boxItem(label, width, clicked, hot);
    Skin::button(Painter::window(), s.button, box.Min, box.Max, hot, label, end);
    return clicked;
}

int buttonRow(std::initializer_list<const char*> labels, float width, float gap)
{
    const Style::ButtonRowStyle& s = Style::get().buttonRow;
    if (width <= 0.0f)
        for (const char* l : labels) width = std::max(width, std::round(ImGui::CalcTextSize(l, ImGui::FindRenderedTextEnd(l)).x + 2.0f * s.labelPadding));
    if (gap <= 0.0f) gap = s.gap;
    const float n = float(labels.size()), row = n * width + (n - 1.0f) * gap;
    ImGui::SetCursorPosX(std::round(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - row) * 0.5f)));
    int clicked = -1, i = 0;
    for (const char* l : labels) {
        if (i > 0) ImGui::SameLine(0.0f, gap);
        if (button(l, width)) clicked = i;
        ++i;
    }
    return clicked;
}

bool iconButton(const char* id, Icon icon)
{
    const Style::Styles& s = Style::get();
    bool clicked = false, hot = false;
    const ImRect box = boxItem(id, s.metrics.boxHeight, clicked, hot);
    Skin::iconButton(Painter::window(), s.button, s.icon, box.Min, box.Max, hot, icon);
    return clicked;
}

bool swatch(const char* id, const float rgba[4])
{
    const Style::Styles& s = Style::get();
    bool clicked = false, hot = false;
    const ImRect box = boxItem(id, s.metrics.boxHeight, clicked, hot);
    const float solid[4] = { rgba[0], rgba[1], rgba[2], 1.0f };
    Skin::swatch(Painter::window(), s.swatch, box.Min, box.Max, Color8::fromUnit(solid).packed(), hot);
    return clicked;
}

bool radio(const char* label, bool active)
{
    const Style::Styles& s = Style::get();
    const ToggleItem t = toggleItem(label);
    const Painter p = Painter::window();
    if (t.hasLabel) Skin::toggleLabel(p, s.toggle, t.label, label, t.end);
    Skin::radio(p, s.toggle, t.mark, active, t.hovered);
    return t.pressed;
}

bool checkbox(const char* label, bool& v)
{
    const Style::Styles& s = Style::get();
    const ToggleItem t = toggleItem(label);
    if (t.pressed) v = !v;
    const Painter p = Painter::window();
    if (t.hasLabel) Skin::toggleLabel(p, s.toggle, t.label, label, t.end);
    Skin::checkbox(p, s.toggle, t.mark, v, t.hovered);
    return t.pressed;
}

}
