#include "ui/widgets/interaction.hpp"

using namespace Widgets::detail;

namespace Widgets {

bool slider(const char* id, float& v, float min, float max, float width, bool logarithmic, float defaultValue)
{
    const Style::Styles& s = Style::get();
    const Range range(min, max, logarithmic);
    float t = 0.0f;
    Track tr{};
    bool changed = false;
    if (trackItem(id, width, t, tr, s.slider.reach())) {
        const float nv = std::clamp(range.fromT(t), min, max);
        if (nv != v) { v = nv; changed = true; }
    }
    changed |= resetOnDoubleClick(v, min, max, defaultValue);

    const float xv = std::round(tr.x0 + (tr.x1 - tr.x0) * std::clamp(range.toT(v), 0.0f, 1.0f));
    Skin::slider(Painter::window(), s.slider, tr.x0, tr.x1, tr.cy, xv, tr.hot);
    return changed;
}

bool figureSlider(const char* id, float& v, float min, float max, float width, SliderFigure figure, bool logarithmic, float defaultValue)
{
    const Style::Styles& s = Style::get();
    const Range range(min, max, logarithmic);
    float t = 0.0f;
    Track tr{};
    bool changed = false;
    if (trackItem(id, width, t, tr, s.figureSlider.margin)) {
        const float nv = std::clamp(range.fromT(t), min, max);
        if (nv != v) { v = nv; changed = true; }
    }
    changed |= resetOnDoubleClick(v, min, max, defaultValue);

    const auto at = [&](float x) { return std::round(tr.x0 + (tr.x1 - tr.x0) * std::clamp(range.toT(x), 0.0f, 1.0f)); };
    const float xv = at(v);
    const float from = figure == SliderFigure::Wave ? at(0.0f) : tr.x0;   // a ramp's covered part starts at its left end
    Skin::figureSlider(Painter::window(), s.figureSlider, s.box, tr.x0, tr.x1, tr.cy, xv, from, figure, tr.hot);
    return changed;
}

bool gradientSlider(const char* id, float& v, const ImU32* stops, int count, float width)
{
    const Style::Styles& s = Style::get();
    float t = 0.0f;
    Track tr{};
    bool changed = false;
    if (trackItem(id, width, t, tr, s.gradientSlider.grab.reach()) && t != v) { v = t; changed = true; }
    const float xv = std::round(tr.x0 + (tr.x1 - tr.x0) * std::clamp(v, 0.0f, 1.0f));
    Skin::gradientSlider(Painter::window(), s.gradientSlider, tr.x0, tr.x1, tr.cy, xv, stops, count, tr.hot);
    return changed;
}

// ---------------------------------------------------------------- rows

bool sliderRow(const char* label, float& v, float min, float max, const char* format, bool logarithmic, float defaultValue)
{
    const Style::Metrics& m = Style::metrics();
    ImGui::PushID(label);
    rowLabel(label);
    bool changed = slider("##slider", v, min, max, stretchWidth(m.fieldWidth), logarithmic, defaultValue);
    alignRight(m.fieldWidth);
    changed |= numberField("##field", v, min, max, format);
    ImGui::PopID();
    return changed;
}

}
