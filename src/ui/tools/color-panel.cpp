#include "color-panel.hpp"
#include "ui/theme.hpp"
#include "ui/tools/tool.hpp"
#include "ui/widgets.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr int   kRingSegments = 96;

ImU32 hsv(float h, float s, float v, float a = 1.0f)
{
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}

// The hue at an angle around the ring: red at the top, going clockwise.
float hueAt(float angle) { return std::fmod(angle / (2.0f * kPi) + 0.25f + 1.0f, 1.0f); }
float angleOf(float hue) { return (hue - 0.25f) * 2.0f * kPi; }

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

void ColorPanel::draw(bool* open, ToolColors& colors)
{
    if (!Widgets::beginPanel("Color", open)) { Widgets::endPanel(); return; }
    float* fg = m_editSecondary ? colors.background : colors.foreground;   // the colour being edited
    if (std::memcmp(fg, m_last, sizeof m_last) != 0) syncFrom(fg);

    // The wheel as large as the panel allows, leaving the swatches' corner free at the side and
    // room below for a gap, the codes row, a gap and the three sliders.
    const ImGuiStyle& st = ImGui::GetStyle();
    const float rowsBelow = Widgets::stackHeight(4, 0, 2) + st.ItemSpacing.y;
    const float block = std::round(ImGui::GetFontSize() * 2.6f);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float wheel = std::clamp(std::min(avail.x - 2.0f * (block + st.ItemSpacing.x), avail.y - rowsBelow), 80.0f,
                                   320.0f * Widgets::metrics().unit);
    float h0 = m_h, s0 = m_s, v0 = m_v;
    const ImVec2 top = ImGui::GetCursorScreenPos();
    drawWheel(wheel);
    const ImVec2 below = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(top);   // the swatches sit in the corner beside the wheel
    drawSwatches(colors, block);
    ImGui::SetCursorScreenPos(below);
    if (float* now = m_editSecondary ? colors.background : colors.foreground; now != fg) {   // switched to the other colour
        fg = now;
        syncFrom(fg);
        h0 = m_h, s0 = m_s, v0 = m_v;
    }
    Widgets::sectionGap();
    drawCodes();
    Widgets::sectionGap();
    drawSliders();

    if (m_h != h0 || m_s != s0 || m_v != v0) writeTo(fg);   // a control changed the colour
    Widgets::endPanel();
}

void ColorPanel::drawSwatches(ToolColors& colors, float block)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##swatches", ImVec2(block, block));
    const float sq = std::round(block * 0.64f);
    const ImVec2 front0 = p, front1(p.x + sq, p.y + sq);                                  // primary
    const ImVec2 back0(p.x + block - sq, p.y + block - sq), back1(p.x + block, p.y + block);   // secondary
    const ImVec2 swap0(front1.x + 1.0f, p.y), swap1(p.x + block, back0.y - 1.0f);         // the arrow's corner

    // One hit area: what was clicked is decided by where.
    const ImVec2 m = ImGui::GetIO().MousePos;
    const auto inside = [&](ImVec2 a, ImVec2 b) { return m.x >= a.x && m.x <= b.x && m.y >= a.y && m.y <= b.y; };
    const bool overSwap = inside(swap0, swap1);
    if (ImGui::IsItemClicked()) {
        if (overSwap)                    colors.swap();
        else if (inside(front0, front1)) m_editSecondary = false;
        else if (inside(back0, back1))   m_editSecondary = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(overSwap ? "Swap colors (X)" : "Primary (front) paints, secondary waits\nClick one to edit it here");

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 panel  = ImGui::GetColorU32(ImGuiCol_WindowBg);
    const ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
    const auto swatch = [&](ImVec2 a, ImVec2 b, const float* rgb, bool edited) {
        dl->AddRectFilled(ImVec2(a.x - 2.0f, a.y - 2.0f), ImVec2(b.x + 2.0f, b.y + 2.0f), panel, 4.0f);   // apart from what's behind
        dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(ImVec4(rgb[0], rgb[1], rgb[2], 1.0f)), 3.0f);
        dl->AddRect(a, b, IM_COL32(0, 0, 0, 200), 3.0f, 0, 1.0f);
        dl->AddRect(ImVec2(a.x + 1.0f, a.y + 1.0f), ImVec2(b.x - 1.0f, b.y - 1.0f), IM_COL32(255, 255, 255, 36), 2.0f);   // a black one still shows
        if (edited) dl->AddRect(ImVec2(a.x - 1.5f, a.y - 1.5f), ImVec2(b.x + 1.5f, b.y + 1.5f), accent, 4.0f, 0, 1.5f);
    };
    swatch(back0, back1, colors.background, m_editSecondary);
    swatch(front0, front1, colors.foreground, !m_editSecondary);

    // The swap arrow: a quarter circle with a head at each end.
    const ImU32 ink = ImGui::GetColorU32(overSwap ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const float w = swap1.x - swap0.x, h = swap1.y - swap0.y;
    const ImVec2 a(swap0.x + w * 0.22f, swap0.y + h * 0.30f), b(swap0.x + w * 0.78f, swap0.y + h * 0.82f);
    dl->AddBezierQuadratic(a, ImVec2(b.x, a.y), b, ink, 1.5f);
    const float k = std::max(2.5f, w * 0.16f);
    dl->AddTriangleFilled(ImVec2(a.x - k * 0.2f, a.y - k), ImVec2(a.x - k * 0.2f, a.y + k), ImVec2(a.x - k * 1.1f, a.y), ink);
    dl->AddTriangleFilled(ImVec2(b.x - k, b.y - k * 0.2f), ImVec2(b.x + k, b.y - k * 0.2f), ImVec2(b.x, b.y + k * 0.9f), ink);
}

void ColorPanel::drawWheel(float size)
{
    const float x = std::max(0.0f, (ImGui::GetContentRegionAvail().x - size) * 0.5f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##wheel", ImVec2(size, size));

    const ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
    const float outer = size * 0.5f - 1.0f;
    const float inner = outer * 0.80f;
    const float half  = inner * 0.70f;   // the square, inscribed with a margin
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

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // The hue ring: quads whose corners carry the hue at their angle.
    const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
    dl->PrimReserve(kRingSegments * 6, kRingSegments * 4);
    for (int i = 0; i < kRingSegments; ++i) {
        const float a0 = 2.0f * kPi * float(i) / kRingSegments, a1 = 2.0f * kPi * float(i + 1) / kRingSegments;
        const ImU32 c0 = hsv(hueAt(a0), 1.0f, 1.0f), c1 = hsv(hueAt(a1), 1.0f, 1.0f);
        const ImVec2 d0(std::cos(a0), std::sin(a0)), d1(std::cos(a1), std::sin(a1));
        const ImDrawIdx base = ImDrawIdx(dl->_VtxCurrentIdx);
        dl->PrimWriteVtx(ImVec2(c.x + d0.x * inner, c.y + d0.y * inner), white, c0);
        dl->PrimWriteVtx(ImVec2(c.x + d0.x * outer, c.y + d0.y * outer), white, c0);
        dl->PrimWriteVtx(ImVec2(c.x + d1.x * outer, c.y + d1.y * outer), white, c1);
        dl->PrimWriteVtx(ImVec2(c.x + d1.x * inner, c.y + d1.y * inner), white, c1);
        dl->PrimWriteIdx(base);     dl->PrimWriteIdx(ImDrawIdx(base + 1)); dl->PrimWriteIdx(ImDrawIdx(base + 2));
        dl->PrimWriteIdx(base);     dl->PrimWriteIdx(ImDrawIdx(base + 2)); dl->PrimWriteIdx(ImDrawIdx(base + 3));
    }
    // Antialiased edges.
    const ImU32 edge = ImGui::GetColorU32(ImGuiCol_WindowBg);
    dl->AddCircle(c, outer + 0.5f, edge, kRingSegments, 1.5f);
    dl->AddCircle(c, inner - 0.5f, edge, kRingSegments, 1.5f);

    // The square: white -> the pure hue left to right, then darkened towards the bottom.
    const ImU32 pure = hsv(m_h, 1.0f, 1.0f);
    dl->AddRectFilledMultiColor(q0, q1, IM_COL32_WHITE, pure, pure, IM_COL32_WHITE);
    dl->AddRectFilledMultiColor(q0, q1, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32_BLACK, IM_COL32_BLACK);

    // Markers: rings, light inside dark, readable on any colour.
    const auto marker = [&](ImVec2 at, float r) {
        dl->AddCircle(at, r + 1.0f, IM_COL32(0, 0, 0, 200), 0, 2.0f);
        dl->AddCircle(at, r, IM_COL32_WHITE, 0, 1.5f);
    };
    const float ringMid = (inner + outer) * 0.5f, ringR = (outer - inner) * 0.30f;
    marker(ImVec2(c.x + std::cos(angleOf(m_h)) * ringMid, c.y + std::sin(angleOf(m_h)) * ringMid), ringR);
    marker(ImVec2(q0.x + m_s * (q1.x - q0.x), q0.y + (1.0f - m_v) * (q1.y - q0.y)), std::max(4.0f, ringR * 0.8f));
}

void ColorPanel::drawCodes()
{
    // [#hex, stretching] [R] [G] [B], the last flush right.
    using namespace Widgets;
    const Metrics m = metrics();
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

void ColorPanel::drawSliders()
{
    // [letter] [gradient] [value]: hue in degrees (0..360, both ends kept), the others in percent.
    using namespace Widgets;
    const Metrics m = metrics();
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
}
