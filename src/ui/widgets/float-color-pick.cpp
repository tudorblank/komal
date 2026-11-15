#include "ui/widgets/float-color-pick.hpp"
#include "ui/render/painter.hpp"
#include "ui/render/skins.hpp"
#include "ui/style/ui-style.hpp"
#include "ui/widgets/hsv.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <utility>

void FloatColorPick::open(Color8 color)
{
    const auto f = color.floats();
    Hsv::follow(f.data(), m_h, m_s, m_v);
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
    const ImVec2 size(width, std::round(width * style.floatSquare));
    ImGui::InvisibleButton("##square", size);
    if (ImGui::IsItemActive()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        m_s = std::clamp((mouse.x - p.x) / size.x, 0.0f, 1.0f);
        m_v = 1.0f - std::clamp((mouse.y - p.y) / size.y, 0.0f, 1.0f);
    }
    finished();
    Skin::colorSquare(Painter::window(), style, p, ImVec2(p.x + size.x, p.y + size.y), Hsv::packed(m_h, 1.0f, 1.0f), m_s, m_v, style.floatMarker);

    // Hue and alpha: [letter] [gradient].
    const float track = width - m.shortLabelWidth - m.spacing;
    ImU32 hue[Hsv::kHueStops];
    Hsv::hueStops(hue);
    rowLabel("H", m.shortLabelWidth);
    gradientSlider("##hue", m_h, hue, Hsv::kHueStops, track);
    finished();
    // A color picked while it's fully see-through: solid, so it shows.
    if (m_a == 0.0f && (m_h != h0 || m_s != s0 || m_v != v0)) m_a = 1.0f;
    const ImU32 alpha[2] = { Hsv::packed(m_h, m_s, m_v, 0.0f), Hsv::packed(m_h, m_s, m_v, 1.0f) };   // see-through -> solid
    rowLabel("A", m.shortLabelWidth);
    gradientSlider("##alpha", m_a, alpha, 2, track);
    finished();

    // The code and the alpha, typed: [#hex] [A %].
    float rgb[3];
    ImGui::ColorConvertHSVtoRGB(m_h, m_s, m_v, rgb[0], rgb[1], rgb[2]);
    if (hexField("##hex", rgb, width - m.fieldWidth - m.spacing)) {
        Hsv::follow(rgb, m_h, m_s, m_v);
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
