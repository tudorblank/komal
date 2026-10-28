#pragma once
// Canvas tools (Photoshop style): the Toolbox picks one, Tool Properties shows its settings, the
// canvas hands it the pointer. A tool draws nothing on the canvas itself; what it changes shows
// through the project (a stroke) or the camera (the hand). It edits the document and its own
// settings only through actions (core/document-actions, ui/app-actions).

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
    const Project&  project;
    ActionRegistry& actions;
    CanvasCamera& camera;
    Vec2 mouse;      // where the pointer is now, viewport-local points
    Vec2 world;      // ... in canvas pixels
    // Every pointer event of the frame, in order, while the canvas has the pointer or the tool is busy.
    std::span<const CanvasPointer> pointer;
    const InputFrame& input;   // the frame as a whole (pen state)
    Vec2 viewSize;
    bool hovered;    // the mouse is over the canvas
    bool active;     // a button went down on the canvas and is held
};

// What Tool Properties hands a tool.
struct ToolContext {
    Project&                project;
    ActionRegistry&         actions;
    ToolManager&            tools;
    const CanvasNavigation& canvas;   // the canvas view, as last shown
};

class Tool {
public:
    virtual ~Tool() = default;

    virtual const char* name() const = 0;
    virtual SDL_Keycode shortcut() const = 0;   // selects it ("tool.<name>", no modifiers)
    virtual void drawIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color) const = 0;

    virtual void drawProperties(ToolContext& ctx) = 0;   // inside the Tool Properties window
    virtual void canvasInput(const CanvasInput& in) = 0;

    virtual bool busy() const { return false; }   // mid-gesture (a stroke, a drag)
    virtual void cancel(ActionRegistry&) {}       // switched away: finish whatever is going on
};

// The id of the action that selects `tool`: "tool.brush".
inline std::string toolActionId(const Tool& tool)
{
    std::string id = "tool.";
    for (const char* c = tool.name(); *c; ++c) id += char(std::tolower(static_cast<unsigned char>(*c)));
    return id;
}
