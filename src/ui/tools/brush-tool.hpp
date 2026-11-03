#pragma once
#include "input/pointer-capture.hpp"
#include "raster/brush.hpp"
#include "ui/tools/tool-base.hpp"

#include <vector>

// Paints into the active raster from the raw pointer events, with pressure: each stroke is one
// brush.stroke gesture (one undo step). LMB / pen tip paints (erases in Erase mode); RMB, barrel
// button and eraser end erase. Its settings change through its own actions (brush.*, [ and ],
// the eraser's E), which it registers.
class BrushTool : public Tool {
public:
    explicit BrushTool(const ToolColors& colors) : m_colors(colors) {}

    const char* name() const override { return "Brush"; }
    ActionId pickAction() const override { return Actions::ToolBrush; }
    void drawIcon(const Painter& draw, ImVec2 center, float size, ImU32 color) const override;

    void drawProperties(ToolContext& ctx) override;
    void canvasInput(const CanvasInput& in) override;
    void registerActions(ActionRegistry& actions, ToolManager& tools) override;
    void picked() override { m_erase = false; }   // B paints; E picks it erasing

    bool busy() const override { return m_stroke.held(); }
    void finish(ActionRegistry& actions) override;

private:
    void endStroke(ActionRegistry& actions);

    const ToolColors& m_colors;   // the tool manager's
    BrushParams m_params;          // colour and mode are filled in when a stroke starts
    std::vector<StrokePoint> m_points;   // this frame's path
    bool m_erase = false;
    PointerCapture m_stroke;     // the press a stroke follows
    int m_strokePoints = 0;      // so far, for the log
};
