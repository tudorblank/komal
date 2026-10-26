#include "brush-tool.hpp"
#include "ui/tools/ring-cursor.hpp"
#include "core/project.hpp"
#include "canvas/scene.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>

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

void BrushTool::drawProperties(ToolContext&)
{
    using namespace Widgets;
    const Metrics m = metrics();

    rowLabel("Mode");
    if (radio("Paint", !m_erase)) m_erase = false;
    nextGroup();
    if (radio("Erase", m_erase)) m_erase = true;

    sliderRow("Size", m_params.diameter, 1.0f, kMaxDiameter, "%.0f px", true);
    sliderRow("Hardness", m_params.hardness, 0.0f, 1.0f, "%.2f");
    float opacity = m_params.opacity / 255.0f;
    if (sliderRow("Opacity", opacity, 0.0f, 1.0f, "%.2f")) m_params.opacity = uint8_t(opacity * 255.0f + 0.5f);
    sliderRow("Flow", m_params.flow, 0.01f, 1.0f, "%.2f");
    sliderRow("Spacing", m_params.spacing, 0.02f, 1.0f, "%.2f");

    // The primary colour: swatch (opens the Color panel), hex, alpha.
    float* fg = m_colors.foreground;
    rowLabel("Color");
    if (swatch("##swatch", fg)) ImGui::SetWindowFocus("Color");
    sameRow();
    hexField("##hex", fg, stretchWidth(m.fieldWidth));
    alignRight(m.fieldWidth);
    float alpha = fg[3] * 100.0f;
    if (numberField("##alpha", alpha, 0.0f, 100.0f, "%.0f%%", 0.0f, "A")) fg[3] = alpha / 100.0f;

    rowLabel("Edges");
    checkbox("Antialiased", m_params.antialias);
    ImGui::SetItemTooltip("Off: hard pixels (diameter 1 = single pixels).");
    rowLabel("Pressure");
    checkbox("Size", m_params.pressureSize);
    ImGui::SetItemTooltip("Pen pressure scales the brush size.");
    nextGroup();
    checkbox("Flow", m_params.pressureFlow);
    ImGui::SetItemTooltip("Pen pressure scales how much each dab adds.");

    ImGui::Spacing();
    ImGui::TextDisabled("LMB / pen paint   RMB / pen eraser erase   [ ] size");
}

void BrushTool::registerActions(ActionRegistry& actions, std::function<bool()> isActive)
{
    actions.add({ .id = "brush.bigger", .title = "Bigger Brush", .shortcuts = { { SDLK_RIGHTBRACKET } },
                  .scope = ActionScope::Canvas, .repeats = true, .enabled = isActive,
                  .run = [this] { m_params.diameter = std::min(kMaxDiameter, std::round(m_params.diameter * 1.25f + 0.5f)); } });
    actions.add({ .id = "brush.smaller", .title = "Smaller Brush", .shortcuts = { { SDLK_LEFTBRACKET } },
                  .scope = ActionScope::Canvas, .repeats = true, .enabled = isActive,
                  .run = [this] { m_params.diameter = std::max(1.0f, std::round(m_params.diameter / 1.25f)); } });
}

void BrushTool::canvasInput(const CanvasInput& in)
{
    // A stroke (one undo step) runs from a press to the release of the same button, through every sample.
    const auto flush = [&] {
        if (m_stroking && !m_points.empty()) in.project.strokeTo(m_points);
        m_points.clear();
    };
    const auto isPen = [](PointerDevice d) { return d != PointerDevice::Mouse; };
    m_points.clear();
    for (const CanvasPointer& p : in.pointer) {
        const PointerEvent& ev = p.event;
        const StrokePoint at{ p.world.x, p.world.y, ev.pressure };
        if (isPen(ev.device)) m_pressure = in.input.pen.touching ? ev.pressure : 1.0f;
        switch (ev.type) {
        case PointerEvent::Type::Down: {
            // ImGui must have given the canvas this press (not a popup, not another panel).
            if (m_stroking || !in.active || ev.button == PointerButton::Middle) break;
            BrushParams params = m_params;
            const float* c = m_colors.foreground;
            params.color = { uint8_t(c[0] * 255.0f + 0.5f), uint8_t(c[1] * 255.0f + 0.5f), uint8_t(c[2] * 255.0f + 0.5f), uint8_t(c[3] * 255.0f + 0.5f) };
            const bool erase = m_erase || ev.button == PointerButton::Secondary || ev.device == PointerDevice::Eraser;
            params.mode = erase ? CompositeMode::Erase : CompositeMode::Over;
            in.project.beginStroke(params, at);
            m_stroking     = true;
            m_strokeButton = ev.button;
            m_strokePen    = isPen(ev.device);
            break;
        }
        case PointerEvent::Type::Move:
            if (m_stroking && isPen(ev.device) == m_strokePen) m_points.push_back(at);
            break;
        case PointerEvent::Type::Up:
            if (m_stroking && ev.button == m_strokeButton && isPen(ev.device) == m_strokePen) {
                m_points.push_back(at);
                flush();
                cancel(in.project);
            }
            break;
        }
    }
    flush();

    // Safety net: a release the app never saw (focus lost mid-stroke) still ends the stroke.
    const bool held = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right) || in.input.pen.touching;
    if (m_stroking && !held) cancel(in.project);
}

void BrushTool::cancel(Project& project)
{
    if (!m_stroking) return;
    project.endStroke();
    m_stroking = false;
}

float BrushTool::cursorDiameter() const
{
    // While a pen presses, the size its pressure gives the dabs.
    return m_params.pressureSize && m_pressure < 1.0f ? std::max(1.0f, m_params.diameter * m_pressure) : m_params.diameter;
}

bool BrushTool::cursorRing(float zoom, float& radius, bool& erase) const
{
    radius = cursorDiameter() * 0.5f * zoom;
    erase  = m_erase;
    return true;
}

void BrushTool::drawOverlay(ImDrawList* dl, const OverlayContext& ctx, Vec2 world) const
{
    // The ring is the system cursor (no lag). Drawn here: the pixel under the mouse when zoomed
    // in, and a ring too big for a system cursor.
    if (ctx.camera.zoom >= 6.0f) {
        const float px = std::floor(world.x), py = std::floor(world.y);
        const Vec2 a = ctx.toScreen(px, py), b = ctx.toScreen(px + 1.0f, py), c = ctx.toScreen(px + 1.0f, py + 1.0f), d = ctx.toScreen(px, py + 1.0f);
        dl->AddQuad(ImVec2(a.x, a.y), ImVec2(b.x, b.y), ImVec2(c.x, c.y), ImVec2(d.x, d.y), IM_COL32(255, 255, 255, 120));
    }
    const float r = cursorDiameter() * 0.5f * ctx.camera.zoom;
    if (!RingCursors::fits(r)) {
        const Vec2 c = ctx.toScreen(world.x, world.y);
        dl->AddCircle(ImVec2(c.x, c.y), r + 1.0f, IM_COL32(0, 0, 0, 160), 0, 1.5f);
        dl->AddCircle(ImVec2(c.x, c.y), r, m_erase ? IM_COL32(255, 140, 140, 220) : IM_COL32(255, 255, 255, 220), 0, 1.0f);
    }
}
