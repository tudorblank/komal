#include "ui/widgets/float-color-pick.hpp"
#include "ui/render/painter.hpp"
#include "ui/render/skins.hpp"
#include "ui/style/ui-style.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {

ImU32 hsv(float h, float s, float v, float a = 1.0f)
{
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}

}

void FloatColorPick::open(Color8 color)
{
    const auto f = color.floats();
    ImGui::ColorConvertRGBtoHSV(f[0], f[1], f[2], m_h, m_s, m_v);
    m_a = f[3];
    m_pending = true;
}

FloatColorPick::Result FloatColorPick::draw(const char* id)
{
    if (std::exchange(m_pending, false)) ImGui::OpenPopup(id);
    Result result;
    m_shown = ImGui::BeginPopup(id);
    if (!m_shown) return result;
    using namespace Widgets;
    const Style::ColorPickerStyle& style = Style::get().colorPicker;
    const Style::Metrics& m = Style::metrics();
    const float width = style.floatWidth;
    const auto finished = [&] { if (ImGui::IsItemDeactivated()) result.done = true; };   // a drag let go
    const float h0 = m_h, s0 = m_s, v0 = m_v;

    // The square: saturation across, brightness down.
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, std::round(width * 0.7f));
    ImGui::InvisibleButton("##square", size);
    if (ImGui::IsItemActive()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        m_s = std::clamp((mouse.x - p.x) / size.x, 0.0f, 1.0f);
        m_v = 1.0f - std::clamp((mouse.y - p.y) / size.y, 0.0f, 1.0f);
    }
    finished();
    Skin::colorSquare(Painter::window(), style, p, ImVec2(p.x + size.x, p.y + size.y), hsv(m_h, 1.0f, 1.0f), m_s, m_v, style.squareMarkerMin + 1.0f);

    // Hue and alpha: [letter] [gradient].
    const float track = width - m.shortLabelWidth - m.spacing;
    ImU32 hue[7];
    for (int i = 0; i < 7; ++i) hue[i] = hsv(float(i) / 6.0f, 1.0f, 1.0f);
    rowLabel("H", m.shortLabelWidth);
    gradientSlider("##hue", m_h, hue, 7, track);
    finished();
    // A colour picked while it's fully see-through: solid, so it shows.
    if (m_a == 0.0f && (m_h != h0 || m_s != s0 || m_v != v0)) m_a = 1.0f;
    const ImU32 alpha[2] = { hsv(m_h, m_s, m_v, 0.0f), hsv(m_h, m_s, m_v, 1.0f) };   // see-through -> solid
    rowLabel("A", m.shortLabelWidth);
    gradientSlider("##alpha", m_a, alpha, 2, track);
    finished();

    // The code and the alpha, typed: [#hex] [A %].
    float rgb[3];
    ImGui::ColorConvertHSVtoRGB(m_h, m_s, m_v, rgb[0], rgb[1], rgb[2]);
    if (hexField("##hex", rgb, width - m.fieldWidth - m.spacing)) {
        float h, s, v;
        ImGui::ColorConvertRGBtoHSV(rgb[0], rgb[1], rgb[2], h, s, v);
        if (v > 0.0f && s > 0.0f) m_h = h;   // grey or black has no hue: keep the one it had
        if (v > 0.0f) m_s = s;
        m_v = v;
        if (m_a == 0.0f) m_a = 1.0f;          // typed while fully see-through: solid
        result.done = true;
    }
    sameRow();
    float percent = std::round(m_a * 100.0f);
    if (numberField("##a", percent, 0.0f, 100.0f, "%.0f%%", m.fieldWidth, "A")) {
        m_a = std::clamp(percent / 100.0f, 0.0f, 1.0f);
        result.done = true;
    }

    ImGui::ColorConvertHSVtoRGB(m_h, m_s, m_v, rgb[0], rgb[1], rgb[2]);
    const float rgba[4] = { rgb[0], rgb[1], rgb[2], m_a };
    result.color = Color8::fromUnit(rgba);
    ImGui::EndPopup();
    return result;
}
