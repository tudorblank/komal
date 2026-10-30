#include "ui/widgets/detail.hpp"
#include "base/math.hpp"
#include "ui/theme.hpp"

#include <cstdio>
#include <cstring>

using namespace Widgets::detail;

namespace Widgets {

// ---------------------------------------------------------------- controls

bool button(const char* label, float width)
{
    const char* end = ImGui::FindRenderedTextEnd(label);
    if (width <= 0.0f) width = std::round(ImGui::CalcTextSize(label, end).x + 2.0f * ImGui::GetStyle().FramePadding.x);
    bool clicked = false, hot = false;
    const ImRect box = boxItem(label, width, clicked, hot);
    drawBox(box, hot);
    textCentered(box, label, end, ImGui::GetColorU32(ImGuiCol_Text));
    return clicked;
}

bool iconButton(const char* id, Icon icon)
{
    const Metrics m = metrics();
    bool clicked = false, hot = false;
    const ImRect box = boxItem(id, m.boxHeight, clicked, hot);
    drawBox(box, hot);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c = box.GetCenter();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    const float s = m.boxHeight * 0.22f, t = std::max(1.25f, m.boxHeight * 0.08f);
    switch (icon) {
    case Icon::Plus:
        dl->AddLine(ImVec2(c.x, c.y - s), ImVec2(c.x, c.y + s), color, t);
        [[fallthrough]];
    case Icon::Minus:
        dl->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), color, t);
        break;
    case Icon::RotateLeft:
    case Icon::RotateRight: {
        // ↻: an open circle, gap at the top, its arrowhead there pointing clockwise. ↺ mirrors it.
        const float mirror = icon == Icon::RotateLeft ? -1.0f : 1.0f;
        const auto at = [&](ImVec2 p) { return ImVec2(c.x + (p.x - c.x) * mirror, p.y + s * 0.15f); };
        const float r = s * 1.1f, a0 = -kPi * 0.11f, a1 = a0 + kPi * 1.62f;
        for (int i = 0; i <= 20; ++i) {
            const float k = a0 + (a1 - a0) * float(i) / 20.0f;
            dl->PathLineTo(at(ImVec2(c.x + std::cos(k) * r, c.y + std::sin(k) * r)));
        }
        dl->PathStroke(color, 0, t);
        const ImVec2 end(c.x + std::cos(a1) * r, c.y + std::sin(a1) * r);
        const ImVec2 along(-std::sin(a1), std::cos(a1)), out(std::cos(a1), std::sin(a1));   // clockwise tangent, outwards
        const float h = s * 0.75f;
        dl->AddTriangleFilled(at(ImVec2(end.x + along.x * h, end.y + along.y * h)),
                              at(ImVec2(end.x + out.x * h * 0.65f - along.x * h * 0.2f, end.y + out.y * h * 0.65f - along.y * h * 0.2f)),
                              at(ImVec2(end.x - out.x * h * 0.65f - along.x * h * 0.2f, end.y - out.y * h * 0.65f - along.y * h * 0.2f)), color);
        break;
    }
    }
    return clicked;
}

bool swatch(const char* id, const float rgba[4])
{
    const Metrics m = metrics();
    bool clicked = false, hot = false;
    const ImRect box = boxItem(id, m.boxHeight, clicked, hot);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(box.Min, box.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(rgba[0], rgba[1], rgba[2], 1.0f)), m.rounding);
    dl->AddRect(box.Min, box.Max, hot ? kToggleHot : Palette::kDarkRim.packed(), m.rounding, 0, 1.0f);
    return clicked;
}

bool radio(const char* label, bool active)
{
    const float d = std::round(ImGui::GetFontSize() * 0.8f);
    bool pressed = false, hovered = false;
    const ImVec2 c = toggleItem(label, d, pressed, hovered);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = d * 0.5f;
    dl->AddCircleFilled(c, r, ImGui::GetColorU32(ImGuiCol_FrameBg));
    dl->AddCircle(c, r - 0.5f, active ? accent() : hovered ? kToggleHot : kToggleEdge, 0, 1.0f);
    if (active) dl->AddCircleFilled(c, r * 0.45f, accent());
    return pressed;
}

bool checkbox(const char* label, bool& v)
{
    const float d = std::round(ImGui::GetFontSize() * 0.8f);
    bool pressed = false, hovered = false;
    const ImVec2 c = toggleItem(label, d, pressed, hovered);
    if (pressed) v = !v;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = d * 0.5f, round = 2.0f * metrics().unit;
    const ImVec2 a(c.x - r, c.y - r), b(c.x + r, c.y + r);
    if (v) {
        dl->AddRectFilled(a, b, accent(), round);
        dl->PathLineTo(ImVec2(c.x - r * 0.5f, c.y + r * 0.02f));
        dl->PathLineTo(ImVec2(c.x - r * 0.12f, c.y + r * 0.42f));
        dl->PathLineTo(ImVec2(c.x + r * 0.55f, c.y - r * 0.4f));
        dl->PathStroke(Palette::kCheckMark.packed(), 0, std::max(1.5f, d * 0.12f));
    } else {
        dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBg), round);
        dl->AddRect(a, b, hovered ? kToggleHot : kToggleEdge, round, 0, 1.0f);
    }
    return pressed;
}

}
