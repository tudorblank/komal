#include "brush-tool.hpp"
#include "core/document-actions.hpp"
#include "core/project.hpp"
#include "canvas/scene.hpp"
#include "ui/panel-titles.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>

void BrushTool::drawIcon(ImDrawList* dl, ImVec2 c, float size, ImU32 color) const
{
    // A round brush: handle down to the ferrule, the tip below it.
    const float s = size * 0.5f;
    dl->AddLine(ImVec2(c.x + s * 0.75f, c.y - s * 0.75f), ImVec2(c.x - s * 0.05f, c.y + s * 0.05f), color, std::max(1.5f, s * 0.22f));
    dl->AddQuadFilled(ImVec2(c.x - s * 0.05f, c.y - s * 0.20f), ImVec2(c.x + s * 0.20f, c.y + s * 0.05f),
                      ImVec2(c.x - s * 0.10f, c.y + s * 0.35f), ImVec2(c.x - s * 0.35f, c.y + s * 0.10f), color);
    dl->AddTriangleFilled(ImVec2(c.x - s * 0.35f, c.y + s * 0.10f), ImVec2(c.x - s * 0.10f, c.y + s * 0.35f),
                          ImVec2(c.x - s * 0.80f, c.y + s * 0.80f), color);
}

void BrushTool::drawProperties(ToolContext& ctx)
{
    using namespace Widgets;
    const Metrics m = metrics();
    ActionRegistry& actions = ctx.actions;
    // Each control edits a copy; a change runs the setting's action (which writes it here).
    const auto row = [&](const char* label, const char* action, float value, float min, float max, const char* format, bool log = false) {
        if (sliderRow(label, value, min, max, format, log)) actions.run(action, { { "value", value } });
    };

    rowLabel("Mode");
    if (radio("Paint", !m_erase)) actions.run("brush.mode", { { "erase", false } });
    nextGroup();
    if (radio("Erase", m_erase)) actions.run("brush.mode", { { "erase", true } });

    row("Size", "brush.size", m_params.diameter, 1.0f, kMaxDiameter, "%.0f px", true);
    row("Hardness", "brush.hardness", m_params.hardness, 0.0f, 1.0f, "%.2f");
    row("Opacity", "brush.opacity", m_params.opacity / 255.0f, 0.0f, 1.0f, "%.2f");
    row("Flow", "brush.flow", m_params.flow, 0.01f, 1.0f, "%.2f");
    row("Spacing", "brush.spacing", m_params.spacing, 0.02f, 1.0f, "%.2f");

    // The primary colour: swatch (opens the Color panel), hex, alpha.
    const float* fg = m_colors.foreground;
    const auto setColor = [&](const float (&c)[4]) {
        actions.run("color.set", { { "target", "primary" }, { "r", c[0] }, { "g", c[1] }, { "b", c[2] }, { "a", c[3] } });
    };
    rowLabel("Color");
    if (swatch("##swatch", fg)) ImGui::SetWindowFocus(PanelTitle::kColor);
    sameRow();
    float c[4] = { fg[0], fg[1], fg[2], fg[3] };
    if (hexField("##hex", c, stretchWidth(m.fieldWidth))) setColor(c);
    alignRight(m.fieldWidth);
    float alpha = fg[3] * 100.0f;
    if (numberField("##alpha", alpha, 0.0f, 100.0f, "%.0f%%", 0.0f, "A")) {
        c[3] = alpha / 100.0f;
        setColor(c);
    }

    const auto toggle = [&](const char* label, const char* action, bool value, const char* tip) {
        if (checkbox(label, value)) actions.run(action, { { "on", value } });
        ImGui::SetItemTooltip("%s", tip);
    };
    rowLabel("Edges");
    toggle("Antialiased", "brush.antialias", m_params.antialias, "Off: hard pixels (diameter 1 = single pixels).");
    rowLabel("Pressure");
    toggle("Size", "brush.pressureSize", m_params.pressureSize, "Pen pressure scales the brush size.");
    nextGroup();
    toggle("Flow", "brush.pressureFlow", m_params.pressureFlow, "Pen pressure scales how much each dab adds.");

    ImGui::Spacing();
    ImGui::TextDisabled("LMB / pen paint   RMB / pen eraser erase   [ ] size");
}

void BrushTool::canvasInput(const CanvasInput& in)
{
    // A stroke (one undo step) runs from a press to the release of the same button, through every sample.
    const auto flush = [&] {
        if (m_stroking && !m_points.empty()) {
            ActionArgs path;
            path.payload = std::span<const StrokePoint>(m_points);
            in.actions.updateGesture("brush.stroke", path);
            m_strokePoints += int(m_points.size());
        }
        m_points.clear();
    };
    const auto isPen = [](PointerDevice d) { return d != PointerDevice::Mouse; };
    m_points.clear();
    for (const CanvasPointer& p : in.pointer) {
        const PointerEvent& ev = p.event;
        const StrokePoint at{ p.world.x, p.world.y, ev.pressure };
        switch (ev.type) {
        case PointerEvent::Type::Down: {
            // ImGui must have given the canvas this press (not a popup, not another panel).
            if (m_stroking || !in.active || ev.button == PointerButton::Middle) break;
            StrokeStart start{ m_params, at };
            const float* c = m_colors.foreground;
            start.params.color = { uint8_t(c[0] * 255.0f + 0.5f), uint8_t(c[1] * 255.0f + 0.5f), uint8_t(c[2] * 255.0f + 0.5f), uint8_t(c[3] * 255.0f + 0.5f) };
            const bool erase = m_erase || ev.button == PointerButton::Secondary || ev.device == PointerDevice::Eraser;
            start.params.mode = erase ? CompositeMode::Erase : CompositeMode::Over;
            char color[16];
            std::snprintf(color, sizeof color, "#%02X%02X%02X%02X", start.params.color.r, start.params.color.g, start.params.color.b, start.params.color.a);
            ActionArgs args{ { "raster", in.project.activeRasterId() }, { "mode", erase ? "erase" : "paint" },
                             { "size", start.params.diameter }, { "color", color }, { "device", isPen(ev.device) ? "pen" : "mouse" } };
            args.payload = start;
            m_stroking = in.actions.beginGesture("brush.stroke", std::move(args));
            m_strokeButton = ev.button;
            m_strokePen    = isPen(ev.device);
            m_strokePoints = 1;
            break;
        }
        case PointerEvent::Type::Move:
            if (m_stroking && isPen(ev.device) == m_strokePen) m_points.push_back(at);
            break;
        case PointerEvent::Type::Up:
            if (m_stroking && ev.button == m_strokeButton && isPen(ev.device) == m_strokePen) {
                m_points.push_back(at);
                flush();
                cancel(in.actions);
            }
            break;
        }
    }
    flush();

    // Safety net: a release the app never saw (focus lost mid-stroke) still ends the stroke.
    if (m_stroking && !in.input.anyButtonHeld()) cancel(in.actions);
}

void BrushTool::cancel(ActionRegistry& actions)
{
    if (!m_stroking) return;
    m_stroking = false;
    actions.endGesture("brush.stroke", { { "points", m_strokePoints } });
}
