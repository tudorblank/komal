#pragma once
#include "core/actions.hpp"
#include "ui/canvas-panel.hpp"
#include "ui/graph-panel/panel.hpp"
#include "ui/dockspace.hpp"
#include "ui/history-panel.hpp"
#include "ui/image-importer.hpp"
#include "ui/info-panel.hpp"
#include "ui/navigator.hpp"
#include "ui/tools/color-panel.hpp"
#include "ui/tools/tool-manager.hpp"
#include "ui/tools/toolbox-panel.hpp"
#include <webgpu/webgpu.h>
#include <string>
#include <utility>
#include <vector>

class GpuDevice;
class Project;
struct InputFrame;
struct SDL_Window;

class UserInterface {
public:
    bool init(GpuDevice& gpu, Project& project, const InputFrame& input);   // call after ImGui + wgpu are up
    void shutdown();              // call before they go down

    void draw();                                              // ImGui side
    void renderGpu(WGPUCommandEncoder enc);                   // wgpu side (offscreen passes)
    void setUiScale(float scale) { m_graphPanel.setUiScale(scale); }

    bool quitRequested() const { return m_quit; }
    // When the UI next needs a frame without new input (see Application::nextFrameIn): tiles still
    // syncing, a throttled picture or thumbnail, a hold turning into a gesture.
    double nextFrameIn() const;
    void setGpuInfo(std::string s) { m_info.setGpuInfo(std::move(s)); }
    void setTimings(float acquireMs, float cpuMs) { m_info.setTimings(acquireMs, cpuMs); }
    void setWindow(SDL_Window* window) { m_window = window; }
    void openFile(std::string path) { m_importer.open(std::move(path)); }   // any thread: File > Import, command line
    // A file dropped at window (x, y): imported if it lands on the canvas or the graph.
    void dropFile(std::string path, float x, float y) { m_drops.push_back({ std::move(path), ImVec2(x, y) }); }

private:
    void drawMenuBar();
    void registerAppActions();   // ui/app-actions.cpp: every app-state action
    void dispatchDrops();

    ActionRegistry      m_actions;   // first: the panels and tools register into it
    ToolManager         m_tools;     // before the panels that use it
    
    CanvasPanel         m_canvasPanel;
    GraphPanel          m_graphPanel;
    ToolboxPanel        m_toolbox;
    ToolPropertiesPanel m_toolProperties;
    ColorPanel          m_colorPanel;
    Navigator           m_navigator;
    InfoPanel           m_info;
    HistoryPanel        m_historyPanel;
    Dockspace           m_dockspace;

    ImageImporter       m_importer;
    struct Drop { std::string path; ImVec2 pos; };
    std::vector<Drop>   m_drops;
    
    Project*            m_project = nullptr;
    const InputFrame*   m_input = nullptr;
    SDL_Window*         m_window = nullptr;

    bool m_initialized = false;
    bool m_quit = false;
    bool m_showDemo = false;
};
