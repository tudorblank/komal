#pragma once
// Canvas tools, Photoshop-style: the Toolbox picks one, the Tool Properties window shows the
// picked tool's settings, and the canvas hands it the mouse. A tool keeps its own settings
// and never draws UI on the canvas beyond its cursor overlay.

#include "core/input.h"
#include "gfx/canvas-camera.h"

#include <imgui.h>
#include <span>

class Project;
class ToolManager;
struct OverlayContext;

// What the canvas publishes each frame for tools that navigate it (the hand's 100% / Fit).
struct CanvasNavigation {
    CanvasCamera* camera = nullptr;   // null while the canvas isn't shown
    float viewW = 0.0f, viewH = 0.0f;
};

// The colours tools share (the Toolbox swatch edits them).
struct ToolColors {
    float foreground[4] = { 1.0f, 1.0f, 1.0f, 1.0f };   // straight alpha
};

// A pointer event (mouse or pen, straight from SDL) with its position on the canvas.
struct CanvasPointer {
    PointerEvent event;
    Vec2         world;   // canvas pixels
};

// The pointer over the canvas, for one frame.
struct CanvasInput {
    Project&      project;
    CanvasCamera& camera;
    Vec2 mouse;      // viewport-local points (ImGui's, once per frame: hover, cursors)
    Vec2 world;      // canvas pixels
    // Every pointer event of the frame, in order, while the canvas has the pointer (hovered,
    // owning a drag, or the tool busy). Precise tools (the brush) work from these.
    std::span<const CanvasPointer> pointer;
    const InputFrame& input;   // the frame as a whole (pen state)
    Vec2 viewSize;
    bool hovered;    // the mouse is over the canvas
    bool active;     // ImGui gave the canvas this click (a button went down on it and is held)
};

struct ToolContext {
    Project&                project;
    ToolManager&            tools;
    const CanvasNavigation& canvas;
};

class Tool {
public:
    virtual ~Tool() = default;

    virtual const char* name() const = 0;
    virtual SDL_Keycode shortcut() const = 0;   // selects it in the Toolbox (no modifiers)
    virtual void drawIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color) const = 0;

    virtual void drawProperties(ToolContext& ctx) = 0;   // inside the Tool Properties window
    virtual void canvasInput(const CanvasInput& in) = 0;
    virtual void paintOverlay(ImDrawList*, const OverlayContext&, Vec2 /*world*/) const {}
    virtual ImGuiMouseCursor cursor() const { return ImGuiMouseCursor_Arrow; }
    // A system-drawn ring cursor (moves with no frame of lag, see RingCursors): `radius` in
    // screen points for a canvas `zoom`. False: cursor() and paintOverlay() only.
    virtual bool cursorRing(float /*zoom*/, float& /*radius*/, bool& /*erase*/) const { return false; }

    virtual bool busy() const { return false; }   // mid-gesture (a stroke, a drag)
    virtual void cancel(Project&) {}              // switched away: finish whatever is going on
};
