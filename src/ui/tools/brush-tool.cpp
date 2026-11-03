#include "brush-tool.hpp"
#include "document/document-actions.hpp"
#include "document/project.hpp"
#include "ui/panels/canvas/scene.hpp"
#include "ui/style/ui-style.hpp"
#include "ui/tools/tool-manager.hpp"
#include "ui/widgets/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace {

// The brush settings' (brush.*): a slider's value, a toggle, the mode.
constexpr ArgKey<float> kValue{ "value" };
constexpr ArgKey<bool>  kOn{ "on" };
constexpr ArgKey<bool>  kErase{ "erase" };

}

void BrushTool::drawIcon(const Painter& draw, ImVec2 c, float size, ImU32 color) const
{
    // A round brush: handle down to the ferrule, the tip below it.
    const float s = size * 0.5f;
    draw.line(ImVec2(c.x + s * 0.75f, c.y - s * 0.75f), ImVec2(c.x - s * 0.05f, c.y + s * 0.05f), color, std::max(1.5f, s * 0.22f));
    draw.fillQuad(ImVec2(c.x - s * 0.05f, c.y - s * 0.20f), ImVec2(c.x + s * 0.20f, c.y + s * 0.05f),
                  ImVec2(c.x - s * 0.10f, c.y + s * 0.35f), ImVec2(c.x - s * 0.35f, c.y + s * 0.10f), color);
    draw.fillTriangle(ImVec2(c.x - s * 0.35f, c.y + s * 0.10f), ImVec2(c.x - s * 0.10f, c.y + s * 0.35f),
                      ImVec2(c.x - s * 0.80f, c.y + s * 0.80f), color);
}

void BrushTool::drawProperties(ToolContext& ctx)
{
    using namespace Widgets;
    const Style::Metrics& m = Style::metrics();
    ActionRegistry& actions = ctx.actions;
    // Each control edits a copy; a change runs the setting's action (which writes it here).
    const auto row = [&](const char* label, ActionId action, float value, float min, float max, const char* format, bool log = false) {
        if (sliderRow(label, value, min, max, format, log)) actions.run(action, { { kValue, value } });
    };

    rowLabel("Mode");
    if (radio("Paint", !m_erase)) actions.run(Actions::BrushMode, { { kErase, false } });
    nextGroup();
    if (radio("Erase", m_erase)) actions.run(Actions::BrushMode, { { kErase, true } });

    row("Size", Actions::BrushSize, m_params.diameter, BrushParams::kMinDiameter, BrushParams::kMaxDiameter, "%.0f px", true);
    row("Hardness", Actions::BrushHardness, m_params.hardness, 0.0f, 1.0f, "%.2f");
    row("Opacity", Actions::BrushOpacity, m_params.opacity / 255.0f, 0.0f, 1.0f, "%.2f");
    row("Flow", Actions::BrushFlow, m_params.flow, BrushParams::kMinFlow, 1.0f, "%.2f");
    row("Spacing", Actions::BrushSpacing, m_params.spacing, BrushParams::kMinSpacing, 1.0f, "%.2f");

    // The primary colour: swatch (opens the Color panel), hex, alpha.
    const float* fg = m_colors.foreground;
    const auto setColor = [&](const float (&c)[4]) {
        using namespace ColorArgs;
        actions.run(Actions::ColorSet, { { kTarget, "primary" }, { kR, c[0] }, { kG, c[1] }, { kB, c[2] }, { kA, c[3] } });
    };
    rowLabel("Color");
    if (swatch("##swatch", fg)) actions.run(Actions::ColorEdit);
    sameRow();
    float c[4] = { fg[0], fg[1], fg[2], fg[3] };
    if (hexField("##hex", c, stretchWidth(m.fieldWidth))) setColor(c);
    alignRight(m.fieldWidth);
    float alpha = fg[3] * 100.0f;
    if (numberField("##alpha", alpha, 0.0f, 100.0f, "%.0f%%", 0.0f, "A")) {
        c[3] = alpha / 100.0f;
        setColor(c);
    }

    const auto toggle = [&](const char* label, ActionId action, bool value, const char* tip) {
        if (checkbox(label, value)) actions.run(action, { { kOn, value } });
        ImGui::SetItemTooltip("%s", tip);
    };
    rowLabel("Edges");
    toggle("Antialiased", Actions::BrushAntialias, m_params.antialias, "Off: hard pixels (diameter 1 = single pixels).");
    rowLabel("Pressure");
    toggle("Size", Actions::BrushPressureSize, m_params.pressureSize, "Pen pressure scales the brush size.");
    nextGroup();
    toggle("Flow", Actions::BrushPressureFlow, m_params.pressureFlow, "Pen pressure scales how much each dab adds.");

    ImGui::Spacing();
    hint(actions, { "LMB / pen paint   RMB / pen eraser erase   ", Actions::BrushSmaller, " ", Actions::BrushBigger, " size" });
}

void BrushTool::registerActions(ActionRegistry& actions, ToolManager& tools)
{
    using K = ActionKind;
    actions.add({ .id = Actions::ToolEraser, .title = "Eraser", .kind = K::AppState,
                  .enabled = [&tools] { return !tools.active().busy(); },
                  .run = [this, &tools, &actions] {
                      tools.select(*this, actions);
                      m_erase = true;
                  } });

    // Set from Tool Properties: a slider drag or a held key logs once, with where it ended. The
    // ranges are the settings' own: a value outside one is refused before it gets here.
    const auto setting = [&actions](ActionId id, const char* title, ArgSpec arg, auto apply) {
        actions.add({ .id = id, .title = title, .kind = K::AppState, .args = { std::move(arg) }, .coalesce = true, .run = std::move(apply) });
    };
    BrushParams& bp = m_params;
    setting(Actions::BrushSize,     "Brush Size",     Arg::number(kValue, BrushParams::kMinDiameter, BrushParams::kMaxDiameter),
            [&bp](const ActionArgs& a) { bp.diameter = a.get(kValue); });
    setting(Actions::BrushHardness, "Brush Hardness", Arg::number(kValue, 0.0, 1.0), [&bp](const ActionArgs& a) { bp.hardness = a.get(kValue); });
    setting(Actions::BrushOpacity,  "Brush Opacity",  Arg::number(kValue, 0.0, 1.0), [&bp](const ActionArgs& a) { bp.opacity = unitToByte(a.get(kValue)); });
    setting(Actions::BrushFlow,     "Brush Flow",     Arg::number(kValue, BrushParams::kMinFlow, 1.0), [&bp](const ActionArgs& a) { bp.flow = a.get(kValue); });
    setting(Actions::BrushSpacing,  "Brush Spacing",  Arg::number(kValue, BrushParams::kMinSpacing, 1.0), [&bp](const ActionArgs& a) { bp.spacing = a.get(kValue); });
    setting(Actions::BrushAntialias,    "Antialiased Edges", Arg::flag(kOn), [&bp](const ActionArgs& a) { bp.antialias = a.get(kOn); });
    setting(Actions::BrushPressureSize, "Pressure Size",     Arg::flag(kOn), [&bp](const ActionArgs& a) { bp.pressureSize = a.get(kOn); });
    setting(Actions::BrushPressureFlow, "Pressure Flow",     Arg::flag(kOn), [&bp](const ActionArgs& a) { bp.pressureFlow = a.get(kOn); });
    setting(Actions::BrushMode,     "Brush Mode",     Arg::flag(kErase), [this](const ActionArgs& a) { m_erase = a.get(kErase); });

    // [ and ] while the brush is the canvas's tool: keys for brush.size (which logs where it ended).
    const auto active = [this, &tools] { return &tools.active() == this; };
    actions.add({ .id = Actions::BrushBigger, .title = "Bigger Brush", .kind = K::Interface,
                  .scope = ActionScope::Canvas, .repeats = true, .enabled = active, .run = [this, &actions] {
                      actions.run(Actions::BrushSize, { { kValue, std::min(BrushParams::kMaxDiameter, std::round(m_params.diameter * 1.25f + 0.5f)) } });
                  } });
    actions.add({ .id = Actions::BrushSmaller, .title = "Smaller Brush", .kind = K::Interface,
                  .scope = ActionScope::Canvas, .repeats = true, .enabled = active, .run = [this, &actions] {
                      actions.run(Actions::BrushSize, { { kValue, std::max(BrushParams::kMinDiameter, std::round(m_params.diameter / 1.25f)) } });
                  } });
}

void BrushTool::canvasInput(const CanvasInput& in)
{
    // A stroke (one undo step) runs from a press to the release of the same button, through every sample.
    const auto flush = [&] {
        if (!m_points.empty()) {   // only gathered while a stroke is held
            ActionArgs path;
            path.payload = std::span<const StrokePoint>(m_points);
            in.actions.updateGesture(Actions::BrushStroke, path);
            m_strokePoints += int(m_points.size());
        }
        m_points.clear();
    };
    m_points.clear();
    for (const CanvasPointer& p : in.pointer) {
        const PointerEvent& ev = p.event;
        const StrokePoint at{ p.world.x, p.world.y, ev.pressure };
        // ImGui must have given the canvas the press (not a popup, not another panel).
        switch (m_stroke.feed(ev, in.active && ev.button != PointerButton::Middle)) {
        case PointerCapture::Step::Began: {
            BrushParams brush = m_params;
            brush.color = Color8::fromUnit(m_colors.foreground);
            const bool erase = m_erase || ev.button == PointerButton::Secondary || ev.device == PointerDevice::Eraser;
            brush.mode = erase ? CompositeMode::Erase : CompositeMode::Over;
            if (!in.actions.beginGesture(Actions::BrushStroke, strokeArgs(in.project.activeRasterId(), brush, at, m_stroke.pen())))
                m_stroke.release();   // refused (a locked raster): nothing to follow
            m_strokePoints = 1;
            break;
        }
        case PointerCapture::Step::Moved: m_points.push_back(at); break;
        case PointerCapture::Step::Ended:
            m_points.push_back(at);
            flush();
            endStroke(in.actions);
            break;
        case PointerCapture::Step::Ignored: break;
        }
    }
    flush();
    if (m_stroke.lost(in.input)) endStroke(in.actions);
}

void BrushTool::finish(ActionRegistry& actions)
{
    if (!m_stroke.held()) return;
    m_stroke.release();
    endStroke(actions);
}

void BrushTool::endStroke(ActionRegistry& actions)
{
    actions.endGesture(Actions::BrushStroke, { { DocArgs::kPoints, m_strokePoints } });
}
