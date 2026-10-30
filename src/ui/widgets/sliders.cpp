#include "ui/widgets/detail.hpp"
#include "base/math.hpp"

using namespace Widgets::detail;

namespace Widgets {

bool slider(const char* id, float& v, float min, float max, float width, bool logarithmic, float defaultValue)
{
    const bool log = logarithmic && min > 0.0f && max > min;
    const auto toT   = [&](float x) { return log ? std::log(x / min) / std::log(max / min) : (x - min) / (max - min); };
    const auto fromT = [&](float t) { return log ? min * std::pow(max / min, t) : min + (max - min) * t; };

    float t = 0.0f;
    Track tr{};
    bool changed = false;
    if (trackItem(id, width, t, tr)) {
        const float nv = std::clamp(fromT(t), min, max);
        if (nv != v) { v = nv; changed = true; }
    }
    if (!std::isnan(defaultValue) && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        v = std::clamp(defaultValue, min, max);   // after the click above moved it
        changed = true;
        ImGui::ClearActiveID();                   // no drag while the button is still down
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float th = std::max(2.0f, std::round(3.0f * metrics().unit));
    const float xv = std::round(tr.x0 + (tr.x1 - tr.x0) * std::clamp(toT(v), 0.0f, 1.0f));
    dl->AddRectFilled(ImVec2(tr.x0, tr.cy - th * 0.5f), ImVec2(tr.x1, tr.cy + th * 0.5f), kTrack, th * 0.5f);
    dl->AddRectFilled(ImVec2(tr.x0, tr.cy - th * 0.5f), ImVec2(xv, tr.cy + th * 0.5f), kTrackFill, th * 0.5f);
    drawGrab(xv, tr.cy, tr.hot);
    return changed;
}

bool figureSlider(const char* id, float& v, float min, float max, float width, SliderFigure figure, bool logarithmic, float defaultValue)
{
    const bool log = logarithmic && min > 0.0f && max > min;
    const auto toT   = [&](float x) { return log ? std::log(x / min) / std::log(max / min) : (x - min) / (max - min); };
    const auto fromT = [&](float t) { return log ? min * std::pow(max / min, t) : min + (max - min) * t; };

    const Metrics m = metrics();
    const float grabHalfW = std::round(3.0f * m.unit) + 1.0f;   // the standard grab, with its rim
    float t = 0.0f;
    Track tr{};
    bool changed = false;
    if (trackItem(id, width, t, tr, grabHalfW + std::round(2.0f * m.unit))) {
        const float nv = std::clamp(fromT(t), min, max);
        if (nv != v) { v = nv; changed = true; }
    }
    if (!std::isnan(defaultValue) && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        v = std::clamp(defaultValue, min, max);   // after the click above moved it
        changed = true;
        ImGui::ClearActiveID();                   // no drag while the button is still down
    }

    // A field's box, slimmer than the fields beside it; the grab a little taller than the box.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float margin = grabHalfW + std::round(2.0f * m.unit);
    const float boxH   = std::round(m.boxHeight * 0.62f) + 1.0f;
    const ImRect box(ImVec2(tr.x0 - margin, tr.cy - boxH * 0.5f), ImVec2(tr.x1 + margin, tr.cy + boxH * 0.5f));
    drawBox(box, tr.hot);

    // The figure, filled in the track's grey; the part the value covers in the slider blue: up to
    // the grab, or for a value either side of zero (a Wave), from the middle to it.
    const float pad = std::round(2.0f * m.unit);
    const float fx0 = box.Min.x + pad, fx1 = box.Max.x - pad, fy0 = box.Min.y + pad, fy1 = box.Max.y - pad;
    const auto drawFigure = [&](ImU32 col) {
        if (figure == SliderFigure::Ramp) {   // a wedge: a sliver at the left, the full height at the right
            const float sliver = std::max(1.0f, std::round(m.unit));
            dl->AddQuadFilled(ImVec2(fx0, fy1 - sliver), ImVec2(fx1, fy0), ImVec2(fx1, fy1), ImVec2(fx0, fy1), col);
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
        dl->AddConcavePolyFilled(pts, kSteps + 3, col);
    };
    const float xv = std::round(tr.x0 + (tr.x1 - tr.x0) * std::clamp(toT(v), 0.0f, 1.0f));
    const float from = figure == SliderFigure::Wave ? std::round(tr.x0 + (tr.x1 - tr.x0) * std::clamp(toT(0.0f), 0.0f, 1.0f)) : fx0;
    drawFigure(kTrack);
    dl->PushClipRect(ImVec2(std::min(from, xv), fy0), ImVec2(std::max(from, xv), fy1), true);
    drawFigure(kTrackFill);
    dl->PopClipRect();

    drawGrab(xv, tr.cy, tr.hot, std::round(m.boxHeight * 0.38f));
    return changed;
}

bool gradientSlider(const char* id, float& v, const ImU32* stops, int count, float width)
{
    float t = 0.0f;
    Track tr{};
    bool changed = false;
    if (trackItem(id, width, t, tr) && t != v) { v = t; changed = true; }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float th = std::round(8.0f * metrics().unit);
    const float y0 = tr.cy - th * 0.5f, y1 = tr.cy + th * 0.5f, len = tr.x1 - tr.x0;
    for (int i = 0; i + 1 < count; ++i) {
        const float a = tr.x0 + len * float(i) / float(count - 1), b = tr.x0 + len * float(i + 1) / float(count - 1);
        dl->AddRectFilledMultiColor(ImVec2(a, y0), ImVec2(b, y1), stops[i], stops[i + 1], stops[i + 1], stops[i]);
    }
    // Rounded ends: the panel colour drawn over the gradient's corners.
    dl->AddRect(ImVec2(tr.x0 - 1.0f, y0 - 1.0f), ImVec2(tr.x1 + 1.0f, y1 + 1.0f), ImGui::GetColorU32(ImGuiCol_WindowBg), th * 0.5f + 1.0f, 0, 2.0f);
    drawGrab(std::round(tr.x0 + len * std::clamp(v, 0.0f, 1.0f)), tr.cy, tr.hot);
    return changed;
}

// ---------------------------------------------------------------- rows

bool sliderRow(const char* label, float& v, float min, float max, const char* format, bool logarithmic, float defaultValue)
{
    const Metrics m = metrics();
    ImGui::PushID(label);
    rowLabel(label);
    bool changed = slider("##slider", v, min, max, stretchWidth(m.fieldWidth), logarithmic, defaultValue);
    alignRight(m.fieldWidth);
    changed |= numberField("##field", v, min, max, format);
    ImGui::PopID();
    return changed;
}

}
