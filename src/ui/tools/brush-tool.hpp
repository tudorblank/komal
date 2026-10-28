#pragma once
#include "raster/brush.hpp"
#include "ui/tools/tool.hpp"

#include <vector>

// Paints into the active raster (one stroke = one undo step) from the raw pointer events, with
// pressure. LMB / pen tip paints (erases in Erase mode); RMB, barrel button and eraser end erase.
// [ and ] resize.
class BrushTool : public Tool {
public:
    explicit BrushTool(ToolColors& colors) : m_colors(colors) {}

    const char* name() const override { return "Brush"; }
    SDL_Keycode shortcut() const override { return SDLK_B; }
    void drawIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color) const override;

    void drawProperties(ToolContext& ctx) override;
    void canvasInput(const CanvasInput& in) override;
    void registerActions(ActionRegistry& actions, std::function<bool()> isActive) override;   // [ ] size

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
    PointerButton m_strokeButton = PointerButton::Primary;   // the stroke ends when this one is released
    bool          m_strokePen    = false;                    // ... by the device that started it
};
