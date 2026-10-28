#pragma once
#include "raster/brush.hpp"
#include "ui/tools/tool.hpp"

#include <vector>

// Paints into the active raster from the raw pointer events, with pressure: each stroke is one
// brush.stroke gesture (one undo step). LMB / pen tip paints (erases in Erase mode); RMB, barrel
// button and eraser end erase. Its settings change through app actions (brush.*, [ and ]).
class BrushTool : public Tool {
public:
    static constexpr float kMaxDiameter = 1024.0f;

    explicit BrushTool(ToolColors& colors) : m_colors(colors) {}

    const char* name() const override { return "Brush"; }
    SDL_Keycode shortcut() const override { return SDLK_B; }
    void drawIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color) const override;

    void drawProperties(ToolContext& ctx) override;
    void canvasInput(const CanvasInput& in) override;

    bool busy() const override { return m_stroking; }
    void cancel(ActionRegistry& actions) override;

    BrushParams& params() { return m_params; }
    bool erase() const { return m_erase; }
    void setErase(bool erase) { m_erase = erase; }

private:
    ToolColors& m_colors;
    BrushParams m_params;          // colour and mode are filled in when a stroke starts
    std::vector<StrokePoint> m_points;   // this frame's path
    bool m_erase    = false;
    bool m_stroking = false;
    PointerButton m_strokeButton = PointerButton::Primary;   // the stroke ends when this one is released
    bool          m_strokePen    = false;                    // ... by the device that started it
    int           m_strokePoints = 0;                        // so far, for the log
};
