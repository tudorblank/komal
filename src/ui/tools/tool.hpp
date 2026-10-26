#pragma once
// Canvas tools (Photoshop style): the Toolbox picks one, Tool Properties shows its settings, the
// canvas hands it the mouse. On the canvas a tool draws only its cursor overlay.

#include "core/actions.hpp"
#include "core/input.hpp"
#include "canvas/camera.hpp"

#include <imgui.h>
#include <cctype>
#include <functional>
#include <span>
#include <string>
#include <utility>

class Project;
class ToolManager;
struct OverlayContext;

// What the canvas publishes each frame for tools that navigate it (the hand's 100% / Fit).
struct CanvasNavigation {
    CanvasCamera* camera = nullptr;   // null while the canvas isn't shown
    float viewW = 0.0f, viewH = 0.0f;
};

// The colours tools share: the primary paints, the secondary waits; X swaps them.
struct ToolColors {
    float foreground[4] = { 1.0f, 1.0f, 1.0f, 1.0f };   // primary, straight alpha
    float background[4] = { 0.0f, 0.0f, 0.0f, 1.0f };   // secondary

    void swap()
    {
        for (int i = 0; i < 4; ++i) std::swap(foreground[i], background[i]);
    }
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
    // Every pointer event of the frame, in order, while the canvas has the pointer or the tool is busy.
    std::span<const CanvasPointer> pointer;
    const InputFrame& input;   // the frame as a whole (pen state)
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
    virtual SDL_Keycode shortcut() const = 0;   // selects it ("tool.<name>", no modifiers)
    // The tool's own actions (usually ActionScope::Canvas); `isActive`: it's the canvas's tool now.
    virtual void registerActions(ActionRegistry&, std::function<bool()> /*isActive*/) {}
    virtual void drawIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color) const = 0;

    virtual void drawProperties(ToolContext& ctx) = 0;   // inside the Tool Properties window
    virtual void canvasInput(const CanvasInput& in) = 0;
    virtual void drawOverlay(ImDrawList*, const OverlayContext&, Vec2 /*world*/) const {}
    virtual ImGuiMouseCursor cursor() const { return ImGuiMouseCursor_Arrow; }
    // A system ring cursor (RingCursors), `radius` in screen points at `zoom`. False: cursor() only.
    virtual bool cursorRing(float /*zoom*/, float& /*radius*/, bool& /*erase*/) const { return false; }

    virtual bool busy() const { return false; }   // mid-gesture (a stroke, a drag)
    virtual void cancel(Project&) {}              // switched away: finish whatever is going on
};

// The id of the action that selects `tool`: "tool.brush".
inline std::string toolActionId(const Tool& tool)
{
    std::string id = "tool.";
    for (const char* c = tool.name(); *c; ++c) id += char(std::tolower(static_cast<unsigned char>(*c)));
    return id;
}
