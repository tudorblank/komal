#pragma once
#include "ui/canvas-panel/canvas-panel.h"
#include "ui/graph-view/graph-panel.h"
#include "ui/image-importer.h"
#include "ui/tools/tool-manager.h"
#include "ui/tools/toolbox-panel.h"
#include <webgpu/webgpu.h>
#include <string>
#include <utility>

class GpuContext;
class Project;
struct SDL_Window;

class UserInterface {
public:
    bool init(GpuContext& gpu, Project& project);   // call after ImGui + wgpu are up
    void shutdown();              // call before they go down

    void draw();                                              // ImGui side
    void renderGpu(WGPUCommandEncoder enc);                   // wgpu side (offscreen passes)
    void setUiScale(float scale) { m_graphPanel.setUiScale(scale); }

    bool quitRequested() const { return m_quit; }
    void setGpuInfo(std::string s) { m_gpuInfo = std::move(s); }
    void setTimings(float acquireMs, float cpuMs) { m_acquireMs = acquireMs; m_cpuMs = cpuMs; }
    void setWindow(SDL_Window* window) { m_window = window; }
    void openFile(std::string path) { m_importer.open(std::move(path)); }   // any thread
    void addMouseSample(float x, float y) { m_tools.addMouseSample(x, y); }

private:
    void drawMenuBar();
    void drawDockspace();
    void drawInfoPanel();
    void handleShortcuts();
    void buildDefaultLayout(unsigned int dockspaceId, float w, float h);

    ToolManager         m_tools;   // before the panels that use it
    CanvasPanel         m_canvasPanel;
    GraphPanel          m_graphPanel;
    ToolboxPanel        m_toolbox;
    ToolPropertiesPanel m_toolProperties;
    ImageImporter       m_importer;
    Project*            m_project = nullptr;
    SDL_Window*         m_window = nullptr;

    bool m_initialized = false;
    bool m_quit = false;
    bool m_resetLayout = false;
    bool m_showCanvas = true, m_showGraph = true, m_showTools = true, m_showToolProperties = true, m_showInfo = true;
    bool m_showDemo = false;

    std::string m_gpuInfo;
    float m_acquireMs = 0, m_cpuMs = 0;
};
