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
    Skin::button(Painter::window(), s.box, box.Min, box.Max, hot, label, end);
    return clicked;
}

bool iconButton(const char* id, Icon icon)
{
    const Style::Styles& s = Style::get();
    bool clicked = false, hot = false;
    const ImRect box = boxItem(id, s.metrics.boxHeight, clicked, hot);
    Skin::iconButton(Painter::window(), s.box, s.icon, box.Min, box.Max, hot, icon);
    return clicked;
}

bool swatch(const char* id, const float rgba[4])
{
    const Style::Styles& s = Style::get();
    bool clicked = false, hot = false;
    const ImRect box = boxItem(id, s.metrics.boxHeight, clicked, hot);
    Skin::swatch(Painter::window(), s.swatch, box.Min, box.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(rgba[0], rgba[1], rgba[2], 1.0f)), hot);
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
