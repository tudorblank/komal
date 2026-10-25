#pragma once
// Canvas tools, Photoshop-style: the Toolbox picks one, the Tool Properties window shows the
// picked tool's settings, and the canvas hands it the mouse. A tool keeps its own settings
// and never draws UI on the canvas beyond its cursor overlay.

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

// The mouse over the canvas, for one frame.
struct CanvasInput {
    Project&      project;
    CanvasCamera& camera;
    Vec2 mouse;      // viewport-local points
    Vec2 world;      // canvas pixels
    std::span<const Vec2> trail;   // canvas pixels: every mouse position this frame, oldest first
    Vec2 viewSize;
    bool hovered;    // the mouse is over the canvas
    bool active;     // a button went down on the canvas and is held
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
    virtual ImGuiKey    shortcut() const = 0;
    virtual void drawIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color) const = 0;

    virtual void drawProperties(ToolContext& ctx) = 0;   // inside the Tool Properties window
    virtual void canvasInput(const CanvasInput& in) = 0;
    virtual void paintOverlay(ImDrawList*, const OverlayContext&, Vec2 /*world*/) const {}
    virtual ImGuiMouseCursor cursor() const { return ImGuiMouseCursor_Arrow; }

    virtual bool busy() const { return false; }   // mid-gesture (a stroke, a drag)
    virtual void cancel(Project&) {}              // switched away: finish whatever is going on
};
