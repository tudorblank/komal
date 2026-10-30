#pragma once
// The app's interface: the menu bar, the dock layout and every panel, and where a dropped file
// goes (the panel under it). Panels never reach each other: they share the project. Panels are
// one list (ui/panel.hpp): their View toggles, close buttons, default layout, draw order, GPU
// work and frames all come from it.

#include "canvas/canvas-engine.hpp"
#include "command/actions.hpp"
#include "ui/dockspace.hpp"
#include "ui/image-importer.hpp"
#include "ui/notices.hpp"
#include "ui/panel.hpp"
#include "ui/tools/tool-manager.hpp"

#include <imgui.h>
#include <webgpu/webgpu.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class CanvasPanel;
class GraphPanel;
class GpuDevice;
class Project;
struct InputFrame;

class UserInterface {
public:
    UserInterface();   // every panel, as each starts (open or not)

    bool init(GpuDevice& gpu, Project& project, const InputFrame& input, const HostInfo& host);   // call after ImGui + wgpu are up
    void shutdown();              // call before they go down

    void draw();                              // ImGui side
    void renderGpu(WGPUCommandEncoder enc);   // wgpu side (offscreen passes)

    bool quitRequested() const { return m_quit; }
    // When the UI next needs a frame without new input (base/frames.hpp): tiles still
    // syncing, a throttled picture or thumbnail, a hold turning into a gesture.
    double nextFrameIn() const;
    void openFile(std::string path) { m_importer.open(std::move(path)); }   // any thread: File > Import, command line
    // A file dropped at window (x, y): imported if it lands on the canvas or the graph.
    void dropFile(std::string path, float x, float y) { m_drops.push_back({ std::move(path), ImVec2(x, y) }); }

private:
    struct PanelEntry {
        std::unique_ptr<Panel> panel;
        PanelInfo              info;   // as it said when added
    };

    template<class T> T* addPanel();
    void drawMenuBar();
    void registerAppActions();   // ui/app-actions.cpp: every app-state action
    void dispatchDrops();

    ActionRegistry m_actions;   // first: the panels and tools register into it
    ToolManager    m_tools;     // before the panels that use it
    CanvasEngine   m_canvas;    // ... and what canvas views draw with
    std::optional<UiContext> m_context;   // what every panel reads (Panel::ui()), from init() on

    std::vector<PanelEntry>  m_panels;      // in View menu order, also their GPU work's (the canvas first)
    std::vector<PanelEntry*> m_drawOrder;   // Edit panels, then Show panels (PanelInfo::phase), each in menu order
    CanvasPanel* m_canvasPanel = nullptr;   // in m_panels: the ones a drop can land on
    GraphPanel*  m_graphPanel  = nullptr;
    Dockspace    m_dockspace;

    ImageImporter       m_importer;
    Notices             m_notices;
    struct Drop { std::string path; ImVec2 pos; };
    std::vector<Drop>   m_drops;

    const InputFrame*   m_input = nullptr;
    const HostInfo*     m_host  = nullptr;

    bool m_initialized = false;
    bool m_quit = false;
};
