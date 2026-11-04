#include "ui/render/skins.hpp"
#include "base/math.hpp"

#include <algorithm>
#include <cmath>

namespace Skin {

// ---------------------------------------------------------------- boxes

void box(const Painter& p, const Style::BoxStyle& s, ImVec2 a, ImVec2 b, bool hot)
{
    p.fillRect(a, b, hot ? s.fillHot : s.fill, s.rounding);
}

void centeredText(const Painter& p, ImVec2 a, ImVec2 b, ImU32 color, const char* text, const char* end)
{
    const ImVec2 t = ImGui::CalcTextSize(text, end);
    const Painter::Clip clip = p.clip(a, b);
    p.text(ImVec2(std::round((a.x + b.x - t.x) * 0.5f), std::round((a.y + b.y - t.y) * 0.5f)), color, text, end);
}

void button(const Painter& p, const Style::BoxStyle& s, ImVec2 a, ImVec2 b, bool hot, const char* label, const char* end)
{
    box(p, s, a, b, hot);
    centeredText(p, a, b, s.text, label, end);
}

void valueBox(const Painter& p, const Style::BoxStyle& s, ImVec2 a, ImVec2 b, bool hot, const char* text, const char* prefix)
{
    box(p, s, a, b, hot);
    ImVec2 inner = a;
    if (prefix) {
        const float pad = s.padding * 0.75f;
        p.text(ImVec2(a.x + pad, std::round((a.y + b.y) * 0.5f - ImGui::GetFontSize() * 0.5f)), s.prefix, prefix);
        inner.x += pad + ImGui::CalcTextSize(prefix).x;
    }
    centeredText(p, inner, b, s.text, text);
}

void textBox(const Painter& p, const Style::BoxStyle& s, ImVec2 a, ImVec2 b, bool hot, const char* text)
{
    box(p, s, a, b, hot);
    const Painter::Clip clip = p.clip(a, b);
    p.text(ImVec2(a.x + s.padding, std::round((a.y + b.y) * 0.5f - ImGui::GetFontSize() * 0.5f)), s.text, text);
}

void iconButton(const Painter& p, const Style::BoxStyle& boxStyle, const Style::IconStyle& icon, ImVec2 a, ImVec2 b, bool hot, Icon shape)
{
    box(p, boxStyle, a, b, hot);
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float h = b.y - a.y;
    const float s = h * icon.size, t = std::max(icon.minStroke, h * icon.stroke);
    switch (shape) {
    case Icon::Plus:
        p.line(ImVec2(c.x, c.y - s), ImVec2(c.x, c.y + s), icon.ink, t);
        [[fallthrough]];
    case Icon::Minus:
        p.line(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), icon.ink, t);
        break;
    case Icon::RotateLeft:
    case Icon::RotateRight: {
        // ↻: an open circle, gap at the top, its arrowhead there pointing clockwise. ↺ mirrors it.
        const float mirror = shape == Icon::RotateLeft ? -1.0f : 1.0f;
        const auto at = [&](ImVec2 q) { return ImVec2(c.x + (q.x - c.x) * mirror, q.y + s * 0.15f); };
        const float r = s * 1.1f, a0 = -kPi * 0.11f, a1 = a0 + kPi * 1.62f;
        ImVec2 points[21];
        for (int i = 0; i <= 20; ++i) {
            const float k = a0 + (a1 - a0) * float(i) / 20.0f;
            points[i] = at(ImVec2(c.x + std::cos(k) * r, c.y + std::sin(k) * r));
        }
        p.polyline(points, icon.ink, t);
        const ImVec2 end(c.x + std::cos(a1) * r, c.y + std::sin(a1) * r);
        const ImVec2 along(-std::sin(a1), std::cos(a1)), out(std::cos(a1), std::sin(a1));   // clockwise tangent, outwards
        const float hh = s * 0.75f;
        p.fillTriangle(at(ImVec2(end.x + along.x * hh, end.y + along.y * hh)),
                       at(ImVec2(end.x + out.x * hh * 0.65f - along.x * hh * 0.2f, end.y + out.y * hh * 0.65f - along.y * hh * 0.2f)),
                       at(ImVec2(end.x - out.x * hh * 0.65f - along.x * hh * 0.2f, end.y - out.y * hh * 0.65f - along.y * hh * 0.2f)), icon.ink);
        break;
    }
    }
}

void swatch(const Painter& p, const Style::SwatchStyle& s, ImVec2 a, ImVec2 b, ImU32 color, bool hot)
{
    p.fillRect(a, b, color, s.rounding);
    p.strokeRect(a, b, hot ? s.rimHot : s.rim, s.rounding, 1.0f);
}

// ---------------------------------------------------------------- toggles

void radio(const Painter& p, const Style::ToggleStyle& s, ImVec2 c, bool on, bool hot)
{
    const float r = s.mark * 0.5f;
    p.fillCircle(c, r, s.fill);
    p.strokeCircle(c, r - 0.5f, on ? s.on : hot ? s.edgeHot : s.edge, 1.0f);
    if (on) p.fillCircle(c, r * s.dot, s.on);
}

void checkbox(const Painter& p, const Style::ToggleStyle& s, ImVec2 c, bool on, bool hot)
{
    const float r = s.mark * 0.5f;
    const ImVec2 a(c.x - r, c.y - r), b(c.x + r, c.y + r);
    if (on) {
        p.fillRect(a, b, s.on, s.checkRounding);
        const ImVec2 tick[] = { ImVec2(c.x - r * 0.5f, c.y + r * 0.02f), ImVec2(c.x - r * 0.12f, c.y + r * 0.42f), ImVec2(c.x + r * 0.55f, c.y - r * 0.4f) };
        p.polyline(tick, s.check, s.checkStroke);
    } else {
        p.fillRect(a, b, s.fill, s.checkRounding);
        p.strokeRect(a, b, hot ? s.edgeHot : s.edge, s.checkRounding, 1.0f);
    }
}

void toggleLabel(const Painter& p, const Style::ToggleStyle& s, ImVec2 at, const char* label, const char* end)
{
    p.text(at, s.label, label, end);
}

// ---------------------------------------------------------------- sliders

void sliderGrab(const Painter& p, const Style::SliderStyle& s, float x, float cy, bool hot)
{
    const float gw = s.grabHalfWidth, gh = s.grabHalfHeight, rim = s.rimWidth;
    p.fillRect(ImVec2(x - gw - rim, cy - gh - rim), ImVec2(x + gw + rim, cy + gh + rim), s.rim, s.rimRounding);
    p.fillRect(ImVec2(x - gw, cy - gh), ImVec2(x + gw, cy + gh), hot ? s.grabHot : s.grab, s.grabRounding);
}

void slider(const Painter& p, const Style::SliderStyle& s, float x0, float x1, float cy, float xv, bool hot)
{
    const float t = s.trackThickness;
    p.fillRect(ImVec2(x0, cy - t * 0.5f), ImVec2(x1, cy + t * 0.5f), s.track, t * 0.5f);
    p.fillRect(ImVec2(x0, cy - t * 0.5f), ImVec2(xv, cy + t * 0.5f), s.fill, t * 0.5f);
    sliderGrab(p, s, xv, cy, hot);
}

void figureSlider(const Painter& p, const Style::FigureSliderStyle& s, const Style::BoxStyle& boxStyle, float x0, float x1, float cy,
                  float xv, float from, Figure figure, bool hot)
{
    // A field's box, slimmer than the fields beside it; the grab a little taller than the box.
    const ImVec2 a(x0 - s.margin, cy - s.boxHeight * 0.5f), b(x1 + s.margin, cy + s.boxHeight * 0.5f);
    box(p, boxStyle, a, b, hot);

    // The figure, filled in the track's grey; the part the value covers in the slider blue: up to
    // the grab, or for a value either side of zero (a Wave), from the middle to it.
    const float fx0 = a.x + s.padding, fx1 = b.x - s.padding, fy0 = a.y + s.padding, fy1 = b.y - s.padding;
    if (figure == Figure::Ramp) from = fx0;
    const auto drawFigure = [&](ImU32 col) {
        if (figure == Figure::Ramp) {   // a wedge: a sliver at the left, the full height at the right
            p.fillQuad(ImVec2(fx0, fy1 - s.sliver), ImVec2(fx1, fy0), ImVec2(fx1, fy1), ImVec2(fx0, fy1), col);
            return;
        }
        // A round bowl on the left (from the top corner down to the bottom, back up to the middle)
        // and the same turned over on the right (up to the top, down to the bottom corner): they
        // meet in the middle, so it runs on as one curve. Stretched with the width, filled down to
        // the bottom as one shape (no seams).
        constexpr int kHalf = 32, kSteps = 2 * kHalf;
        const float h = fy1 - fy0, xm = (fx0 + fx1) * 0.5f;
        // 0..1 across the half -> 0 (top) .. 1 (bottom), ending at 0.5. A sine's roundness: unlike a
        // circle's, its ends aren't vertical, so the middle is crossed on a slope, not a cliff.
        const auto bowl = [](float u) { return 0.5f * u + std::sin(kPi * u) * (1.0f - 0.5f * u); };
        ImVec2 pts[kSteps + 3];
        for (int i = 0; i <= kHalf; ++i) {
            const float u = float(i) / kHalf;
            pts[i]         = ImVec2(fx0 + (xm - fx0) * u, fy0 + bowl(u) * h);
            pts[kHalf + i] = ImVec2(xm + (fx1 - xm) * u, fy0 + (1.0f - bowl(1.0f - u)) * h);   // turned over about the middle
        }
        pts[kSteps + 1] = ImVec2(fx1, fy1);
        pts[kSteps + 2] = ImVec2(fx0, fy1);
        p.fillPolygon(pts, col);
    };
    drawFigure(s.grab.track);
    {
        const Painter::Clip clip = p.clip(ImVec2(std::min(from, xv), fy0), ImVec2(std::max(from, xv), fy1));
        drawFigure(s.grab.fill);
    }
    sliderGrab(p, s.grab, xv, cy, hot);
}

void gradientSlider(const Painter& p, const Style::GradientSliderStyle& s, float x0, float x1, float cy, float xv,
                    const ImU32* stops, int count, bool hot)
{
    const float th = s.thickness;
    const float y0 = cy - th * 0.5f, y1 = cy + th * 0.5f, len = x1 - x0;
    if (std::any_of(stops, stops + count, [](ImU32 c) { return (c >> IM_COL32_A_SHIFT & 0xFF) != 0xFF; }))
        checker(p, ImVec2(x0, y0), ImVec2(x1, y1), s.checkerCell, s.checkerA, s.checkerB);
    for (int i = 0; i + 1 < count; ++i) {
        const float a = x0 + len * float(i) / float(count - 1), b = x0 + len * float(i + 1) / float(count - 1);
        p.gradientRect(ImVec2(a, y0), ImVec2(b, y1), stops[i], stops[i + 1], stops[i + 1], stops[i]);
    }
    // Rounded ends: the panel colour drawn over the gradient's corners.
    p.strokeRect(ImVec2(x0 - 1.0f, y0 - 1.0f), ImVec2(x1 + 1.0f, y1 + 1.0f), s.edge, th * 0.5f + 1.0f, s.edgeWidth);
    sliderGrab(p, s.grab, xv, cy, hot);
}

// ---------------------------------------------------------------- colour pickers

void checker(const Painter& p, ImVec2 a, ImVec2 b, float cell, ImU32 colorA, ImU32 colorB)
{
    p.fillRect(a, b, colorB);
    if (cell <= 0.0f) return;
    for (float y = a.y; y < b.y; y += cell)
        for (float x = a.x; x < b.x; x += cell)
            if ((int((x - a.x) / cell) + int((y - a.y) / cell)) % 2 == 0)
                p.fillRect(ImVec2(x, y), ImVec2(std::min(x + cell, b.x), std::min(y + cell, b.y)), colorA);
}

void colorMarker(const Painter& p, const Style::ColorPickerStyle& s, ImVec2 at, float radius)
{
    p.strokeCircle(at, radius + 1.0f, s.markerRim, s.markerRimWidth);
    p.strokeCircle(at, radius, s.marker, s.markerWidth);
}

void colorSquare(const Painter& p, const Style::ColorPickerStyle& s, ImVec2 a, ImVec2 b, ImU32 pureHue, float sat, float value,
                 float markerRadius)
{
    // White -> the pure hue left to right, then darkened towards the bottom.
    p.gradientRect(a, b, s.white, pureHue, pureHue, s.white);
    p.gradientRect(a, b, s.clear, s.clear, s.black, s.black);
    colorMarker(p, s, ImVec2(a.x + sat * (b.x - a.x), a.y + (1.0f - value) * (b.y - a.y)), markerRadius);
}

// ---------------------------------------------------------------- the Toolbox

void toolButton(const Painter& p, const Style::ToolButtonStyle& s, ImVec2 a, ImVec2 b, bool selected, bool hot)
{
    if (selected || hot) p.fillRect(a, b, selected ? s.fillSelected : s.fillHot, s.rounding);
    if (selected) p.strokeRect(a, b, s.outline, s.rounding);
}

void documentTab(const Painter& p, const Style::DocumentTabStyle& s, ImVec2 a, ImVec2 b, bool shown, bool hot, const char* name,
                 TabMark mark)
{
    if (shown || hot) p.fillRect(a, b, shown ? s.shown : s.hot, s.rounding);
    const ImU32 ink = shown ? s.textShown : s.text;
    const float markX = b.x - s.padding * 0.5f - s.mark, cy = (a.y + b.y) * 0.5f;
    {
        const Painter::Clip clip = p.clip(a, ImVec2(markX, b.y));
        p.text(ImVec2(a.x + s.padding, std::round(cy - ImGui::GetFontSize() * 0.5f)), ink, name);
    }
    const ImVec2 c(markX + s.mark * 0.5f, cy);
    switch (mark) {
    case TabMark::Unsaved:
        p.fillCircle(c, std::max(2.0f, s.mark * 0.18f), ink);
        break;
    case TabMark::CloseHot:
        p.fillRect(ImVec2(markX, cy - s.mark * 0.5f), ImVec2(markX + s.mark, cy + s.mark * 0.5f), s.markHot, s.rounding);
        [[fallthrough]];
    case TabMark::Close: {
        const float r = s.mark * 0.22f, w = std::max(1.0f, s.mark * 0.09f);
        p.line(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ink, w);
        p.line(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), ink, w);
        break;
    }
    }
}

}
