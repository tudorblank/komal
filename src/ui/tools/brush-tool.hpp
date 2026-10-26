#pragma once
#include "raster/brush.hpp"
#include "ui/tools/tool.hpp"

#include <vector>

// Paints into the active raster through the Project's stroke API (one stroke = one undo
// step). LMB / pen tip paints, or erases in Erase mode; RMB / pen barrel button and the pen's
// eraser end always erase. [ and ] resize. A stroke is built from the raw pointer events: the
// exact press, every sample in between with its pressure, the exact release.
class BrushTool : public Tool {
public:
    explicit BrushTool(ToolColors& colors) : m_colors(colors) {}

    const char* name() const override { return "Brush"; }
    SDL_Keycode shortcut() const override { return SDLK_B; }
    void drawIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color) const override;

    void drawProperties(ToolContext& ctx) override;
    void canvasInput(const CanvasInput& in) override;
    void drawOverlay(ImDrawList* dl, const OverlayContext& ctx, Vec2 world) const override;
    bool cursorRing(float zoom, float& radius, bool& erase) const override;   // the brush outline
    void registerActions(ActionRegistry& actions, std::function<bool()> isActive) override;   // [ ] size

    bool busy() const override { return m_stroking; }
    void cancel(Project& project) override;

    void setErase(bool erase) { m_erase = erase; }

private:
    static constexpr float kMaxDiameter = 1024.0f;

    float cursorDiameter() const;   // world pixels: the size a dab has right now (pen pressure)

    ToolColors& m_colors;
    BrushParams m_params;          // colour and mode are filled in when a stroke starts
    std::vector<StrokePoint> m_points;   // this frame's path
    bool m_erase    = false;
    bool m_stroking = false;
    PointerButton m_strokeButton = PointerButton::Primary;   // the stroke ends when this one is released
    bool          m_strokePen    = false;                    // ... by the device that started it
    float         m_pressure     = 1.0f;                     // the pen's, for the cursor
};
