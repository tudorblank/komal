#include "hand-tool.hpp"
#include "document/project.hpp"
#include "ui/widgets/widgets.hpp"

#include <algorithm>
#include <cmath>

void HandTool::drawIcon(ImDrawList* dl, ImVec2 c, float size, ImU32 color) const
{
    // An open hand: four fingers over a palm, the thumb to the left.
    const float s = size * 0.5f;
    const float fw = s * 0.26f;
    const float tops[4] = { -0.55f, -0.80f, -0.75f, -0.50f };
    for (int i = 0; i < 4; ++i) {
        const float x = c.x - s * 0.45f + i * fw * 1.15f;
        dl->AddRectFilled(ImVec2(x, c.y + tops[i] * s), ImVec2(x + fw, c.y + s * 0.1f), color, fw * 0.5f);
    }
    dl->AddRectFilled(ImVec2(c.x - s * 0.47f, c.y - s * 0.05f), ImVec2(c.x + s * 0.47f, c.y + s * 0.80f), color, s * 0.3f);
    dl->AddQuadFilled(ImVec2(c.x - s * 0.47f, c.y + s * 0.15f), ImVec2(c.x - s * 0.85f, c.y - s * 0.20f),
                      ImVec2(c.x - s * 0.70f, c.y - s * 0.35f), ImVec2(c.x - s * 0.30f, c.y - s * 0.05f), color);
}

void HandTool::drawProperties(ToolContext& ctx)
{
    const Vec2 viewport = ctx.project.canvasViewport();
    if (viewport.x <= 0.0f || viewport.y <= 0.0f) {
        ImGui::TextDisabled("The canvas is hidden.");
        return;
    }
    // Zooms about the canvas's middle.
    const CanvasCamera& cam = ctx.project.canvasCamera();
    const auto zoomTo = [&](float zoom) { ctx.actions.run(Actions::CanvasZoom, { { "factor", zoom / cam.zoom } }); };

    float percent = cam.zoom * 100.0f;
    if (Widgets::sliderRow("Zoom", percent, CanvasCamera::kMinZoom * 100.0f, CanvasCamera::kMaxZoom * 100.0f, "%.0f%%", true, 100.0f))
        zoomTo(percent / 100.0f);

    if (Widgets::button("100%")) zoomTo(1.0f);
    Widgets::sameRow();
    if (Widgets::button("Fit on screen")) ctx.actions.run(Actions::CanvasFrameAll);   // F on the canvas

    ImGui::Spacing();
    Widgets::hint(ctx.actions, { "LMB drag pan   Space: hand from any tool" });
    Widgets::hint(ctx.actions, { "MMB drag / wheel pan   ", PrimaryKey{}, "+wheel zoom" });
}

void HandTool::canvasInput(const CanvasInput& in)
{
    // From a primary press on the canvas to its release, by the device that pressed.
    const auto isPen = [](PointerDevice d) { return d != PointerDevice::Mouse; };
    for (const CanvasPointer& p : in.pointer) {
        const PointerEvent& ev = p.event;
        const bool down = ev.type == PointerEvent::Type::Down, up = ev.type == PointerEvent::Type::Up;
        if (!m_dragging) {
            // ImGui must have given the canvas this press (not a popup, not another panel).
            if (down && ev.button == PointerButton::Primary && in.active) {
                m_dragging = true;
                m_dragPen  = isPen(ev.device);
                m_last     = { ev.x, ev.y };
            }
            continue;
        }
        if (isPen(ev.device) != m_dragPen || (!up && ev.type != PointerEvent::Type::Move)) continue;
        if (up && ev.button != PointerButton::Primary) continue;
        if (ev.x != m_last.x || ev.y != m_last.y)
            in.actions.run(Actions::CanvasPan, { { "dx", ev.x - m_last.x }, { "dy", ev.y - m_last.y } });
        m_last = { ev.x, ev.y };
        if (up) m_dragging = false;
    }
    // Safety net: a release the app never saw (focus lost mid-drag) still ends the drag.
    if (m_dragging && !in.input.buttonHeld(m_dragPen ? PointerDevice::Pen : PointerDevice::Mouse, PointerButton::Primary))
        m_dragging = false;
}
