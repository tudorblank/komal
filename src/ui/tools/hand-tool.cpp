#include "hand-tool.hpp"
#include "core/project.hpp"
#include "ui/widgets.hpp"

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
    CanvasCamera* cam = ctx.canvas.camera;
    if (!cam) {
        ImGui::TextDisabled("The canvas is hidden.");
        return;
    }
    const Vec2 center = { ctx.canvas.viewW * 0.5f, ctx.canvas.viewH * 0.5f };

    float percent = cam->zoom * 100.0f;
    if (Widgets::sliderRow("Zoom", percent, CanvasCamera::kMinZoom * 100.0f, CanvasCamera::kMaxZoom * 100.0f, "%.0f%%", true, 100.0f))
        cam->zoomAt(center, percent / 100.0f / cam->zoom);

    if (Widgets::button("100%")) cam->zoomAt(center, 1.0f / cam->zoom);
    Widgets::sameRow();
    if (Widgets::button("Fit on screen")) {
        const BoundsI b = ctx.project.master().bounds();
        if (!b.empty()) cam->frame(float(b.minX), float(b.minY), float(b.maxX + 1), float(b.maxY + 1), ctx.canvas.viewW, ctx.canvas.viewH);
    }

    ImGui::Spacing();
    ImGui::TextDisabled("LMB drag pan   Space: hand from any tool");
    ImGui::TextDisabled("MMB drag / wheel pan   Ctrl+wheel zoom");
}

void HandTool::canvasInput(const CanvasInput& in)
{
    m_dragging = in.active && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const ImVec2 d = ImGui::GetIO().MouseDelta;
    if (m_dragging && (d.x != 0.0f || d.y != 0.0f)) {
        in.camera.pan.x += d.x;
        in.camera.pan.y += d.y;
    }
}
