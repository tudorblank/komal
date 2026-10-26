#include "color-panel.hpp"
#include "ui/theme.hpp"
#include "ui/tools/tool.hpp"

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

// A gradient slider: its track shows `stops` evenly spaced colours, the grab marks `value`
// (0..1). Click or drag anywhere on it. Returns true while it changes the value.
bool gradientSlider(const char* id, const char* label, float& value, const ImU32* stops, int count, const char* valueText)
{
    const ImGuiStyle& st = ImGui::GetStyle();
    const float h      = ImGui::GetFrameHeight();
    const float labelW = ImGui::CalcTextSize("M").x + st.ItemSpacing.x;
    const float valueW = ImGui::CalcTextSize("360°").x + st.ItemSpacing.x;
    const float width  = ImGui::GetContentRegionAvail().x;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelW);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float trackW = std::max(20.0f, width - labelW - valueW);
    ImGui::InvisibleButton(id, ImVec2(trackW, h));
    // The gradient runs between the grab's two extreme positions, so the grab never leaves it.
    const float gw = std::max(3.0f, std::round(h * 0.14f));
    const float x0 = p.x + gw + 1.0f, x1 = p.x + trackW - gw - 1.0f, len = std::max(1.0f, x1 - x0);
    bool changed = false;
    if (ImGui::IsItemActive()) {
        const float v = std::clamp((ImGui::GetIO().MousePos.x - x0) / len, 0.0f, 1.0f);
        if (v != value) { value = v; changed = true; }
    }

    // The track: the gradient, its corners rounded by drawing the panel colour over them.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float trackH = std::round(h * 0.42f);
    const float y0 = p.y + std::round((h - trackH) * 0.5f), y1 = y0 + trackH;
    for (int i = 0; i + 1 < count; ++i) {
        const float a = x0 + len * float(i) / float(count - 1);
        const float b = x0 + len * float(i + 1) / float(count - 1);
        dl->AddRectFilledMultiColor(ImVec2(a, y0), ImVec2(b, y1), stops[i], stops[i + 1], stops[i + 1], stops[i]);
    }
    const float r = trackH * 0.5f;
    dl->AddRect(ImVec2(x0 - 1.0f, y0 - 1.0f), ImVec2(x1 + 1.0f, y1 + 1.0f), ImGui::GetColorU32(ImGuiCol_WindowBg), r + 1.0f, 0, 2.0f);

    // The grab: a light pill across the track, outlined dark so it reads on any colour.
    const float gx = x0 + value * len;
    const ImVec2 g0(gx - gw, p.y + 2.0f), g1(gx + gw, p.y + h - 2.0f);
    dl->AddRectFilled(ImVec2(g0.x - 1.0f, g0.y - 1.0f), ImVec2(g1.x + 1.0f, g1.y + 1.0f), IM_COL32(0, 0, 0, 170), gw + 1.0f);
    dl->AddRectFilled(g0, g1, (ImGui::IsItemActive() || ImGui::IsItemHovered()) ? IM_COL32(255, 255, 255, 255) : IM_COL32(225, 225, 225, 255), gw);

    ImGui::SameLine(labelW + trackW + st.ItemSpacing.x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(valueText);
    return changed;
}

} // namespace

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
    if (!ImGui::Begin("Color", open)) { ImGui::End(); return; }
    float* fg = m_editSecondary ? colors.background : colors.foreground;   // the colour being edited
    if (std::memcmp(fg, m_last, sizeof m_last) != 0) syncFrom(fg);

    // The wheel as large as the panel allows, leaving the swatches' corner free at the side and
    // room for the rows under it.
    const ImGuiStyle& st = ImGui::GetStyle();
    const float rowsH = ImGui::GetFrameHeightWithSpacing() * 4.0f + st.ItemSpacing.y * 2.0f;
    const float block = std::round(ImGui::GetFontSize() * 2.6f);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float wheel = std::clamp(std::min(avail.x - 2.0f * (block + st.ItemSpacing.x), avail.y - rowsH), 80.0f,
                                   320.0f * (ImGui::GetFontSize() / 15.0f));
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
    ImGui::Spacing();
    drawCodes();
    ImGui::Spacing();
    drawSliders();

    if (m_h != h0 || m_s != s0 || m_v != v0) writeTo(fg);   // a control changed the colour
    ImGui::End();
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
    float rgb[3];
    ImGui::ColorConvertHSVtoRGB(m_h, m_s, m_v, rgb[0], rgb[1], rgb[2]);
    int ri[3] = { int(rgb[0] * 255.0f + 0.5f), int(rgb[1] * 255.0f + 0.5f), int(rgb[2] * 255.0f + 0.5f) };

    // #hex: shows the colour, takes a typed one when the field is left.
    const ImGuiStyle& st = ImGui::GetStyle();
    const float width = ImGui::GetContentRegionAvail().x;
    const float hexW  = std::round(width * 0.34f);
    const float chanW = std::floor((width - hexW - st.ItemSpacing.x * 3.0f) / 3.0f);
    if (!m_hexEditing) std::snprintf(m_hex, sizeof m_hex, "%02X%02X%02X", ri[0], ri[1], ri[2]);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("#");
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::SetNextItemWidth(hexW - ImGui::CalcTextSize("#").x - 2.0f);
    {
        NumberFont font;
        ImGui::InputText("##hex", m_hex, sizeof m_hex, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase |
                                                        ImGuiInputTextFlags_AutoSelectAll);
    }
    m_hexEditing = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit() && std::strlen(m_hex) == 6) {
        unsigned v = 0;
        if (std::sscanf(m_hex, "%06x", &v) == 1) {
            const float typed[3] = { float((v >> 16) & 0xFF) / 255.0f, float((v >> 8) & 0xFF) / 255.0f, float(v & 0xFF) / 255.0f };
            syncFrom(typed);   // written out by draw(), with the HSB values
        }
    }

    // R G B, 0..255: drag or type.
    static const char* kNames[3] = { "##red", "##green", "##blue" };
    static const char* kFormats[3] = { "R %d", "G %d", "B %d" };
    bool rgbChanged = false;
    for (int i = 0; i < 3; ++i) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(chanW);
        NumberFont font;
        rgbChanged |= ImGui::DragInt(kNames[i], &ri[i], 0.5f, 0, 255, kFormats[i], ImGuiSliderFlags_AlwaysClamp);
    }
    if (rgbChanged) {
        const float typed[3] = { ri[0] / 255.0f, ri[1] / 255.0f, ri[2] / 255.0f };
        syncFrom(typed);
    }
}

void ColorPanel::drawSliders()
{
    char text[16];

    ImU32 hue[7];   // the full circle, at full saturation and brightness (as Photoshop shows it)
    for (int i = 0; i < 7; ++i) hue[i] = hsv(float(i) / 6.0f, 1.0f, 1.0f);
    std::snprintf(text, sizeof text, "%d°", int(std::lround(m_h * 360.0f)) % 360);
    gradientSlider("##hue", "H", m_h, hue, 7, text);

    const ImU32 sat[2] = { hsv(m_h, 0.0f, m_v), hsv(m_h, 1.0f, m_v) };   // grey -> the full colour
    std::snprintf(text, sizeof text, "%d%%", int(std::lround(m_s * 100.0f)));
    gradientSlider("##saturation", "S", m_s, sat, 2, text);

    const ImU32 bri[2] = { hsv(m_h, m_s, 0.0f), hsv(m_h, m_s, 1.0f) };   // black -> the full colour
    std::snprintf(text, sizeof text, "%d%%", int(std::lround(m_v * 100.0f)));
    gradientSlider("##brightness", "B", m_v, bri, 2, text);
}
