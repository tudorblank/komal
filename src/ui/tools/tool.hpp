#pragma once
// Canvas tools (Photoshop style): the Toolbox picks one, Tool Properties shows its settings, the
// canvas hands it the pointer. A tool draws nothing on the canvas itself; what it changes shows
// through the project: a stroke, or the canvas camera (the hand). It changes the project and its
// own settings only through actions: the document's and the canvas's (document/document-actions)
// and its own, which it registers itself (registerActions). Picking it is ToolManager's, by its
// pickAction().

#include "base/math.hpp"
#include "command/actions.hpp"
#include "command/input.hpp"

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
    Vec2 mouse;      // where the pointer is now, viewport-local points
    Vec2 world;      // ... in canvas pixels
    // Every pointer event of the frame, in order, while the canvas has the pointer or the tool is busy.
    std::span<const CanvasPointer> pointer;
    const InputFrame& input;   // the frame as a whole (pen state)
    bool hovered;    // the mouse is over the canvas
    bool active;     // a button went down on the canvas and is held
};

// What Tool Properties hands a tool.
struct ToolContext {
    const Project&  project;   // to read (the canvas camera too): changes go through actions
    ActionRegistry& actions;
    ToolManager&    tools;
};

class Tool {
public:
    virtual ~Tool() = default;

    virtual const char* name() const = 0;
    virtual ActionId pickAction() const = 0;      // picks it (command/action-ids.hpp)
    virtual SDL_Keycode shortcut() const = 0;   // ... on this key, no modifiers
    virtual void drawIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color) const = 0;

    virtual void drawProperties(ToolContext& ctx) = 0;   // inside the Tool Properties window
    virtual void canvasInput(const CanvasInput& in) = 0;
    virtual void registerActions(ActionRegistry&, ToolManager&) {}   // its settings and their keys, once
    virtual void picked() {}   // picked through its own action (the toolbox, its key): its plain mode again

    virtual bool busy() const { return false; }   // mid-gesture (a stroke, a drag)
    virtual void cancel(ActionRegistry&) {}       // switched away: finish whatever is going on
};
