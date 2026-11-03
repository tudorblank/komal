#include "hand-tool.hpp"
#include "document/document-actions.hpp"
#include "document/project.hpp"
#include "ui/widgets/widgets.hpp"

#include <algorithm>
#include <cmath>

void HandTool::drawIcon(const Painter& draw, ImVec2 c, float size, ImU32 color) const
{
    // An open hand: four fingers over a palm, the thumb to the left.
    const float s = size * 0.5f;
    const float fw = s * 0.26f;
    const float tops[4] = { -0.55f, -0.80f, -0.75f, -0.50f };
    for (int i = 0; i < 4; ++i) {
        const float x = c.x - s * 0.45f + float(i) * fw * 1.15f;
        draw.fillRect(ImVec2(x, c.y + tops[i] * s), ImVec2(x + fw, c.y + s * 0.1f), color, fw * 0.5f);
    }
    draw.fillRect(ImVec2(c.x - s * 0.47f, c.y - s * 0.05f), ImVec2(c.x + s * 0.47f, c.y + s * 0.80f), color, s * 0.3f);
    draw.fillQuad(ImVec2(c.x - s * 0.47f, c.y + s * 0.15f), ImVec2(c.x - s * 0.85f, c.y - s * 0.20f),
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
    const auto zoomTo = [&](float zoom) { ctx.actions.run(Actions::CanvasZoom, { { DocArgs::kFactor, zoom / cam.zoom } }); };

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
    // From a primary press on the canvas to its release. ImGui must have given the canvas the
    // press (not a popup, not another panel).
    for (const CanvasPointer& p : in.pointer) {
        const PointerEvent& ev = p.event;
        switch (m_drag.feed(ev, in.active && ev.button == PointerButton::Primary)) {
        case PointerCapture::Step::Began: m_last = { ev.x, ev.y }; break;
        case PointerCapture::Step::Moved:
        case PointerCapture::Step::Ended:
            if (ev.x != m_last.x || ev.y != m_last.y)
                in.actions.run(Actions::CanvasPan, { { DocArgs::kDx, ev.x - m_last.x }, { DocArgs::kDy, ev.y - m_last.y } });
            m_last = { ev.x, ev.y };
            break;
        case PointerCapture::Step::Ignored: break;
        }
    }
    m_drag.lost(in.input);
}
