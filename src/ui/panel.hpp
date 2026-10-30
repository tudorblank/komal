#pragma once
// A window of the app. UserInterface keeps every panel in one list: each one's title, View
// toggle, close button, place in the default layout, draw order, frames and GPU work all
// come from there. A new panel is one class and one line in that list.
//
//   class MyPanel : public Panel {
//       PanelInfo info() const override { return { Actions::ViewMine, "Mine", DockSlot::RightLower }; }
//       bool init() override;   // what it makes once (ui() is there)
//       void draw() override;   // its window, through Widgets::PanelWindow; ui().project, ui().actions...
//   };

#include "base/frames.hpp"
#include "command/action-ids.hpp"

#include <webgpu/webgpu.h>

#include <string>

struct SDL_Window;

class ActionRegistry;
class CanvasEngine;
class GpuDevice;
class NodeThumbnails;
class Project;
class ToolManager;
struct InputFrame;

// Where a panel goes in the default layout (Dockspace).
enum class DockSlot : unsigned char {
    Toolbar,       // a bar under the menu bar, drawn before the dockspace (not docked)
    Center,        // the canvas
    Bottom,        // under it: the graph
    RightTop,      // the side column, top to bottom
    RightMiddle,
    RightLower,
    RightBottom,   // panels sharing a slot become tabs
    Floating,      // not docked by default
};

// When a panel draws in a frame. Edit panels may change the project (through actions); Show panels
// draw after all of them, so what they show is the project as this frame left it (the canvas: its
// outlines and its GPU picture always agree). Within a phase, order doesn't matter: panels never
// touch each other, only the project.
enum class DrawPhase : unsigned char { Edit, Show };

struct PanelInfo {
    ActionId    toggle;            // its View toggle (command/action-ids.hpp): the menu and its close button run it
    const char* title;             // its window's title (also its ImGui id and its name in the saved layout) and View menu entry
    DockSlot    slot;
    DrawPhase   phase = DrawPhase::Edit;
    bool        openByDefault = true;
};

// What the app tells the interface about where it runs. The app keeps it current; panels read it.
struct HostInfo {
    SDL_Window* window  = nullptr;   // dialogs open over it
    float       uiScale = 1.0f;      // the window's content scale (the style is scaled by it too)
    std::string gpu;                 // the GPU and display, as text
};

// What every panel may use (Panel::ui()), from init() on. Everything lives as long as the panels.
// What they may only read is const: the project (changed through actions), the canvas engine. The
// thumbnails take requests (drawing one asks for it); the tools take the canvas's pointer.
struct UiContext {
    GpuDevice&          gpu;
    const Project&      project;
    ActionRegistry&     actions;
    ToolManager&        tools;
    const InputFrame&   input;
    const CanvasEngine& canvas;
    NodeThumbnails&     thumbnails;
    const HostInfo&     host;
};

class Panel {
public:
    virtual ~Panel() = default;

    virtual PanelInfo info() const = 0;
    virtual bool init() { return true; }              // false: it failed (and shows that, or nothing)
    virtual void shutdown() {}
    virtual void draw() = 0;                          // its window; nothing while closed
    virtual void renderGpu(WGPUCommandEncoder) {}     // its offscreen passes, after every draw
    virtual double nextFrameIn() const { return kNoFrame; }   // base/frames.hpp; kNoFrame while closed

    bool isOpen() const { return m_open; }               // its View toggle (only) changes it
    const char* title() const { return m_title; }        // PanelInfo's
    ActionId toggleAction() const { return m_toggle; }   // its close button runs it

protected:
    const UiContext& ui() const { return *m_ui; }

private:
    friend class UserInterface;
    const UiContext* m_ui = nullptr;   // UserInterface's, set before init()
    bool        m_open = true;
    const char* m_title = "";
    ActionId    m_toggle;
};
