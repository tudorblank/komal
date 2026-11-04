#include "color-panel.hpp"
#include "base/math.hpp"
#include "command/actions.hpp"
#include "ui/render/painter.hpp"
#include "ui/render/skins.hpp"
#include "ui/style/ui-style.hpp"
#include "ui/tools/tool-base.hpp"
#include "ui/tools/tool-manager.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace {

ImU32 hsv(float h, float s, float v, float a = 1.0f)
{
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}

// The hue at an angle around the ring: red at the top, going clockwise.
float hueAt(float angle) { return std::fmod(angle / kTau + 0.25f + 1.0f, 1.0f); }
float angleOf(float hue) { return (hue - 0.25f) * kTau; }

}

void ColorPanel::syncFrom(const float rgb[3])
{
    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(rgb[0], rgb[1], rgb[2], h, s, v);
    if (v > 0.0f && s > 0.0f) m_h = h;   // grey or black has no hue: keep the one we had
    if (v > 0.0f) m_s = s;               // black has no saturation either
    m_v = v;
    std::memcpy(m_last, rgb, sizeof m_last);
}

void ColorPanel::writeTo(float rgb[3])
{
    ImGui::ColorConvertHSVtoRGB(m_h, m_s, m_v, rgb[0], rgb[1], rgb[2]);
    std::memcpy(m_last, rgb, sizeof m_last);
}

PanelInfo ColorPanel::info() const { return { Actions::ViewColor, "Color", DockSlot::RightMiddle }; }

bool ColorPanel::init()
{
    // From anywhere a colour is shown (the brush's swatch): this panel, opened and in front.
    ui().actions.add({ .id = Actions::ColorEdit, .title = "Edit Color", .kind = ActionKind::Interface, .run = [this] {
        if (!isOpen()) ui().actions.run(toggleAction());   // opening is the View toggle's (logged)
        m_focus = true;
    } });
    return true;
}

void ColorPanel::draw()
{
    if (!isOpen()) return;
    const ToolColors& colors = ui().tools.colors();
    ActionRegistry& actions = ui().actions;
    if (std::exchange(m_focus, false)) ImGui::SetNextWindowFocus();
    const Widgets::PanelWindow window(*this, actions);
    if (!window) return;
    const float* fg = m_editSecondary ? colors.background : colors.foreground;   // the colour being edited
    if (std::memcmp(fg, m_last, sizeof m_last) != 0) syncFrom(fg);

    // The wheel as large as the panel allows, leaving the swatches' corner free at the side and
    // room below for a gap, the codes row, a gap and the three sliders.
    const Style::ColorPickerStyle& style = Style::get().colorPicker;
    const ImGuiStyle& st = ImGui::GetStyle();
    const float rowsBelow = Widgets::stackHeight(5, 0, 2) + st.ItemSpacing.y;
    const float block = style.block;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float wheel = std::clamp(std::min(avail.x - 2.0f * (block + st.ItemSpacing.x), avail.y - rowsBelow), style.minSize, style.maxSize);
    float h0 = m_h, s0 = m_s, v0 = m_v;
    const ImVec2 top = ImGui::GetCursorScreenPos();
    drawWheel(wheel);
    const ImVec2 below = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(top);   // the swatches sit in the corner beside the wheel
    drawSwatches(colors, actions, block);
    ImGui::SetCursorScreenPos(below);
    if (const float* now = m_editSecondary ? colors.background : colors.foreground; now != fg) {   // switched to the other colour
        fg = now;
        syncFrom(fg);
        h0 = m_h, s0 = m_s, v0 = m_v;
    }
    Widgets::sectionGap();
    drawCodes();
    Widgets::sectionGap();
    float alpha = fg[3];
    drawSliders(alpha);

    const char* target = m_editSecondary ? "secondary" : "primary";
    if (m_h != h0 || m_s != s0 || m_v != v0) {   // a control changed the colour: its action writes it
        float rgb[3];
        writeTo(rgb);
        actions.run(Actions::ColorSet, { { ColorArgs::kTarget, target }, { ColorArgs::kR, rgb[0] }, { ColorArgs::kG, rgb[1] },
                                         { ColorArgs::kB, rgb[2] } });
    }
    if (alpha != fg[3]) actions.run(Actions::ColorSet, { { ColorArgs::kTarget, target }, { ColorArgs::kA, alpha } });
}

void ColorPanel::drawSwatches(const ToolColors& colors, ActionRegistry& actions, float block)
{
    const Style::ColorPickerStyle& style = Style::get().colorPicker;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##swatches", ImVec2(block, block));
    const float sq = std::round(block * style.swatchShare);
    const ImVec2 front0 = p, front1(p.x + sq, p.y + sq);                                  // primary
    const ImVec2 back0(p.x + block - sq, p.y + block - sq), back1(p.x + block, p.y + block);   // secondary
    const ImVec2 swap0(front1.x + 1.0f, p.y), swap1(p.x + block, back0.y - 1.0f);         // the arrow's corner

    // One hit area: what was clicked is decided by where.
    const ImVec2 m = ImGui::GetIO().MousePos;
    const auto inside = [&](ImVec2 a, ImVec2 b) { return m.x >= a.x && m.x <= b.x && m.y >= a.y && m.y <= b.y; };
    const bool overSwap = inside(swap0, swap1);
    if (ImGui::IsItemClicked()) {
        if (overSwap)                    actions.run(Actions::ColorSwap);
        else if (inside(front0, front1)) m_editSecondary = false;
        else if (inside(back0, back1))   m_editSecondary = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(overSwap ? "Swap colors (X)" : "Primary (front) paints, secondary waits\nClick one to edit it here");

    const Painter draw = Painter::window();
    const float r = style.swatchRounding, gap = style.swatchGap;
    const auto swatch = [&](ImVec2 a, ImVec2 b, const float* rgba, bool edited) {
        draw.fillRect(ImVec2(a.x - gap, a.y - gap), ImVec2(b.x + gap, b.y + gap), style.gap, r + 1.0f);   // apart from what's behind
        if (rgba[3] < 1.0f) Skin::checker(draw, a, b, style.checkerCell, style.checkerA, style.checkerB);   // see-through
        draw.fillRect(a, b, ImGui::ColorConvertFloat4ToU32(ImVec4(rgba[0], rgba[1], rgba[2], rgba[3])), r);
        draw.strokeRect(a, b, style.rim, r, 1.0f);
        draw.strokeRect(ImVec2(a.x + 1.0f, a.y + 1.0f), ImVec2(b.x - 1.0f, b.y - 1.0f), style.lightRim, r - 1.0f);   // a black one still shows
        if (edited) draw.strokeRect(ImVec2(a.x - 1.5f, a.y - 1.5f), ImVec2(b.x + 1.5f, b.y + 1.5f), style.edited, r + 1.0f, style.editedWidth);
    };
    swatch(back0, back1, colors.background, m_editSecondary);
    swatch(front0, front1, colors.foreground, !m_editSecondary);

    // The swap arrow: a quarter circle with a head at each end.
    const ImU32 ink = overSwap ? style.swapHot : style.swap;
    const float w = swap1.x - swap0.x, h = swap1.y - swap0.y;
    const ImVec2 a(swap0.x + w * 0.22f, swap0.y + h * 0.30f), b(swap0.x + w * 0.78f, swap0.y + h * 0.82f);
    draw.bezier(a, ImVec2(b.x, a.y), b, ink, style.swapStroke);
    const float k = std::max(style.swapHeadMin, w * style.swapHead);
    draw.fillTriangle(ImVec2(a.x - k * 0.2f, a.y - k), ImVec2(a.x - k * 0.2f, a.y + k), ImVec2(a.x - k * 1.1f, a.y), ink);
    draw.fillTriangle(ImVec2(b.x - k, b.y - k * 0.2f), ImVec2(b.x + k, b.y - k * 0.2f), ImVec2(b.x, b.y + k * 0.9f), ink);
}

void ColorPanel::drawWheel(float size)
{
    const float x = std::max(0.0f, (ImGui::GetContentRegionAvail().x - size) * 0.5f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##wheel", ImVec2(size, size));

    const Style::ColorPickerStyle& style = Style::get().colorPicker;
    const ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
    const float outer = size * 0.5f - 1.0f;
    const float inner = outer * style.ringInner;
    const float half  = inner * style.square;   // the square, inscribed with a margin
    const ImVec2 q0(c.x - half, c.y - half), q1(c.x + half, c.y + half);

    // Which part a press grabbed; a drag then stays on it.
    const ImVec2 m = ImGui::GetIO().MousePos;
    const float dx = m.x - c.x, dy = m.y - c.y;
    if (ImGui::IsItemActivated()) {
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d >= inner - 2.0f && d <= outer + 2.0f)                      m_wheelDrag = WheelDrag::Hue;
        else if (m.x >= q0.x && m.x <= q1.x && m.y >= q0.y && m.y <= q1.y) m_wheelDrag = WheelDrag::SatBright;
        else                                                             m_wheelDrag = WheelDrag::None;
    }
    if (!ImGui::IsItemActive()) m_wheelDrag = WheelDrag::None;
    if (m_wheelDrag == WheelDrag::Hue) {
        m_h = hueAt(std::atan2(dy, dx));
    } else if (m_wheelDrag == WheelDrag::SatBright) {
        m_s = std::clamp((m.x - q0.x) / (q1.x - q0.x), 0.0f, 1.0f);
        m_v = 1.0f - std::clamp((m.y - q0.y) / (q1.y - q0.y), 0.0f, 1.0f);
    }

    const Painter draw = Painter::window();
    // The hue ring: quads whose corners carry the hue at their angle.
    const int segments = style.ringSegments;
    for (int i = 0; i < segments; ++i) {
        const float a0 = kTau * float(i) / float(segments), a1 = kTau * float(i + 1) / float(segments);
        const ImU32 c0 = hsv(hueAt(a0), 1.0f, 1.0f), c1 = hsv(hueAt(a1), 1.0f, 1.0f);
        const ImVec2 d0(std::cos(a0), std::sin(a0)), d1(std::cos(a1), std::sin(a1));
        const ImVec2 corners[4] = { ImVec2(c.x + d0.x * inner, c.y + d0.y * inner), ImVec2(c.x + d0.x * outer, c.y + d0.y * outer),
                                    ImVec2(c.x + d1.x * outer, c.y + d1.y * outer), ImVec2(c.x + d1.x * inner, c.y + d1.y * inner) };
        const ImU32 colors[4] = { c0, c0, c1, c1 };
        draw.gradientQuad(corners, colors);
    }
    // Antialiased edges.
    draw.strokeCircle(c, outer + 0.5f, style.ringEdge, style.ringEdgeWidth, segments);
    draw.strokeCircle(c, inner - 0.5f, style.ringEdge, style.ringEdgeWidth, segments);

    // The square, and the markers: rings, light inside dark, readable on any colour.
    const float ringMid = (inner + outer) * 0.5f, ringR = (outer - inner) * style.hueMarker;
    Skin::colorSquare(draw, style, q0, q1, hsv(m_h, 1.0f, 1.0f), m_s, m_v, std::max(style.squareMarkerMin, ringR * style.squareMarker));
    Skin::colorMarker(draw, style, ImVec2(c.x + std::cos(angleOf(m_h)) * ringMid, c.y + std::sin(angleOf(m_h)) * ringMid), ringR);
}

void ColorPanel::drawCodes()
{
    // [#hex, stretching] [R] [G] [B], the last flush right.
    using namespace Widgets;
    const Style::Metrics& m = Style::metrics();
    float rgb[3];
    ImGui::ColorConvertHSVtoRGB(m_h, m_s, m_v, rgb[0], rgb[1], rgb[2]);
    if (hexField("##hex", rgb, stretchWidth(3.0f * m.fieldWidth + 2.0f * m.spacing))) syncFrom(rgb);   // written out by draw()

    static const char* kIds[3] = { "##red", "##green", "##blue" };
    static const char* kPrefixes[3] = { "R", "G", "B" };
    for (int i = 0; i < 3; ++i) {
        if (i < 2) sameRow(); else alignRight(m.fieldWidth);
        float channel = std::round(rgb[i] * 255.0f);
        if (numberField(kIds[i], channel, 0.0f, 255.0f, "%.0f", 0.0f, kPrefixes[i])) {
            rgb[i] = std::round(channel) / 255.0f;
            syncFrom(rgb);
        }
    }
}

void ColorPanel::drawSliders(float& alpha)
{
    // [letter] [gradient] [value]: hue in degrees (0..360, both ends kept), the others in percent.
    using namespace Widgets;
    const Style::Metrics& m = Style::metrics();
    const auto row = [&](const char* label, const char* id, float& value, const ImU32* stops, int count, float shown, const char* format) {
        ImGui::PushID(id);
        rowLabel(label, m.shortLabelWidth);
        gradientSlider("##slider", value, stops, count, stretchWidth(m.fieldWidth));
        alignRight(m.fieldWidth);
        float typed = std::round(value * shown);
        if (numberField("##value", typed, 0.0f, shown, format)) value = typed / shown;
        ImGui::PopID();
    };

    ImU32 hue[7];   // the full circle at full saturation and brightness
    for (int i = 0; i < 7; ++i) hue[i] = hsv(float(i) / 6.0f, 1.0f, 1.0f);
    row("H", "hue", m_h, hue, 7, 360.0f, "%.0f°");

    const ImU32 sat[2] = { hsv(m_h, 0.0f, m_v), hsv(m_h, 1.0f, m_v) };   // grey -> the full colour
    row("S", "saturation", m_s, sat, 2, 100.0f, "%.0f%%");

    const ImU32 bri[2] = { hsv(m_h, m_s, 0.0f), hsv(m_h, m_s, 1.0f) };   // black -> the full colour
    row("B", "brightness", m_v, bri, 2, 100.0f, "%.0f%%");

    const ImU32 opacity[2] = { hsv(m_h, m_s, m_v, 0.0f), hsv(m_h, m_s, m_v, 1.0f) };   // see-through -> solid
    row("A", "alpha", alpha, opacity, 2, 100.0f, "%.0f%%");
}
