#include "brush-tool.h"
#include "core/project.h"
#include "gfx/canvas-scene.h"

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
    const float labelW = ImGui::GetFontSize() * 5.0f;
    const auto label = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };

    label("Mode");
    if (ImGui::RadioButton("Paint", !m_erase)) m_erase = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Erase", m_erase)) m_erase = true;

    label("Size");
    ImGui::DragFloat("##size", &m_params.diameter, 0.25f, 1.0f, kMaxDiameter, "%.0f px", ImGuiSliderFlags_Logarithmic);
    label("Hardness");
    ImGui::SliderFloat("##hardness", &m_params.hardness, 0.0f, 1.0f, "%.2f");
    label("Opacity");
    float opacity = m_params.opacity / 255.0f;
    if (ImGui::SliderFloat("##opacity", &opacity, 0.0f, 1.0f, "%.2f")) m_params.opacity = uint8_t(opacity * 255.0f + 0.5f);
    label("Flow");
    ImGui::SliderFloat("##flow", &m_params.flow, 0.01f, 1.0f, "%.2f");
    label("Spacing");
    ImGui::SliderFloat("##spacing", &m_params.spacing, 0.02f, 1.0f, "%.2f");
    label("Color");
    ImGui::ColorEdit4("##color", m_colors.foreground, ImGuiColorEditFlags_AlphaBar);
    label("Edges");
    ImGui::Checkbox("Antialiased", &m_params.antialias);
    ImGui::SetItemTooltip("Off: hard pixels (diameter 1 = single pixels).");

    ImGui::Spacing();
    ImGui::TextDisabled("LMB paint   RMB erase   [ ] size");
}

void BrushTool::canvasInput(const CanvasInput& in)
{
    if (in.hovered && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) m_params.diameter = std::min(kMaxDiameter, std::round(m_params.diameter * 1.25f + 0.5f));
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket))  m_params.diameter = std::max(1.0f, std::round(m_params.diameter / 1.25f));
    }

    // Pressing starts a stroke (and an undo step), dragging extends it, releasing ends it.
    const bool left  = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool right = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const bool painting = in.active && (left || right);
    if (painting && !m_stroking) {
        BrushParams p = m_params;
        const float* c = m_colors.foreground;
        p.color = { uint8_t(c[0] * 255.0f + 0.5f), uint8_t(c[1] * 255.0f + 0.5f), uint8_t(c[2] * 255.0f + 0.5f), uint8_t(c[3] * 255.0f + 0.5f) };
        p.mode  = (m_erase || !left) ? CompositeMode::Erase : CompositeMode::Over;
        in.project.beginStroke(p, in.world.x, in.world.y);
        m_stroking = true;
    } else if (painting) {
        m_points.clear();
        for (Vec2 p : in.trail) m_points.push_back({ p.x, p.y });
        m_points.push_back({ in.world.x, in.world.y });
        in.project.strokeTo(m_points);
    }
    if (m_stroking && !painting) cancel(in.project);
}

void BrushTool::cancel(Project& project)
{
    if (!m_stroking) return;
    project.endStroke();
    m_stroking = false;
}

void BrushTool::paintOverlay(ImDrawList* dl, const OverlayContext& ctx, Vec2 world) const
{
    const Vec2 c = ctx.toScreen(world.x, world.y);
    const ImVec2 m(c.x, c.y);

    if (ctx.camera.zoom >= 6.0f) {   // the pixel under the mouse
        const float px = std::floor(world.x), py = std::floor(world.y);
        const Vec2 a = ctx.toScreen(px, py), b = ctx.toScreen(px + 1.0f, py + 1.0f);
        dl->AddRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), IM_COL32(255, 255, 255, 120));
    }

    const float r = m_params.diameter * 0.5f * ctx.camera.zoom;
    if (r >= 3.0f) {   // dark under light: readable on any content
        dl->AddCircle(m, r + 1.0f, IM_COL32(0, 0, 0, 160), 0, 1.5f);
        dl->AddCircle(m, r, m_erase ? IM_COL32(255, 140, 140, 220) : IM_COL32(255, 255, 255, 220), 0, 1.0f);
    }
    // Always a small crosshair: with the system cursor hidden it marks the exact spot.
    dl->AddLine(ImVec2(m.x - 5, m.y), ImVec2(m.x + 6, m.y), IM_COL32(0, 0, 0, 160), 3.0f);
    dl->AddLine(ImVec2(m.x, m.y - 5), ImVec2(m.x, m.y + 6), IM_COL32(0, 0, 0, 160), 3.0f);
    dl->AddLine(ImVec2(m.x - 5, m.y), ImVec2(m.x + 6, m.y), IM_COL32(255, 255, 255, 220));
    dl->AddLine(ImVec2(m.x, m.y - 5), ImVec2(m.x, m.y + 6), IM_COL32(255, 255, 255, 220));
}
