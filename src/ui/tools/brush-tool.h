#pragma once
#include "raster/brush.h"
#include "ui/tools/tool.h"

#include <vector>

// Paints into the active raster through the Project's stroke API (one stroke = one undo
// step). LMB paints, or erases in Erase mode; RMB always erases. [ and ] resize.
class BrushTool : public Tool {
public:
    explicit BrushTool(ToolColors& colors) : m_colors(colors) {}

    const char* name() const override { return "Brush"; }
    ImGuiKey    shortcut() const override { return ImGuiKey_B; }
    void drawIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color) const override;

    void drawProperties(ToolContext& ctx) override;
    void canvasInput(const CanvasInput& in) override;
    void paintOverlay(ImDrawList* dl, const OverlayContext& ctx, Vec2 world) const override;
    ImGuiMouseCursor cursor() const override { return ImGuiMouseCursor_None; }   // the overlay is the cursor

    bool busy() const override { return m_stroking; }
    void cancel(Project& project) override;

    void setErase(bool erase) { m_erase = erase; }

private:
    static constexpr float kMaxDiameter = 1024.0f;

    ToolColors& m_colors;
    BrushParams m_params;          // colour and mode are filled in when a stroke starts
    std::vector<StrokePoint> m_points;   // this frame's path
    bool m_erase    = false;
    bool m_stroking = false;
};
