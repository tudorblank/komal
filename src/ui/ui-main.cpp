#include "ui-main.h"
#include "core/perf.h"
#include "core/project.h"
#include "gfx/gpu-context.h"

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder* lives here

bool UserInterface::init(GpuContext& gpu, Project& project)
{
    m_project = &project;
    m_graphPanel.init(gpu, project);
    const bool ok = m_canvasPanel.init(gpu, project, m_tools);
    m_initialized = true;
    return ok;
}

void UserInterface::shutdown()
{
    if (!m_initialized) return;
    m_graphPanel.shutdown();
    m_canvasPanel.shutdown();
    m_initialized = false;
}

void UserInterface::renderGpu(WGPUCommandEncoder enc)
{
    m_canvasPanel.renderGpu(enc);
}

void UserInterface::draw()
{
    m_importer.poll(*m_project);
    handleShortcuts();
    drawMenuBar();
    drawDockspace();   // must come before the panels that dock into it

    if (m_showTools)          m_toolbox.draw(&m_showTools, m_tools, *m_project);
    if (m_showToolProperties) m_toolProperties.draw(&m_showToolProperties, m_tools, *m_project);
    if (m_showCanvas)         m_canvasPanel.draw(&m_showCanvas);
    if (m_showGraph) { PerfScope perf(Perf::Graph); m_graphPanel.draw(&m_showGraph); }
    if (m_showInfo)           drawInfoPanel();
    if (m_showDemo)           ImGui::ShowDemoWindow(&m_showDemo);
    m_tools.clearMouseTrail();   // consumed by the canvas this frame
}

void UserInterface::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Import Image...", "Ctrl+O"))
            m_importer.showOpenDialog(m_window);
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Alt+F4"))
            m_quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_project->canUndo()))       m_project->undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, m_project->canRedo())) m_project->redo();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Canvas",          nullptr, &m_showCanvas);
        ImGui::MenuItem("Graph",           nullptr, &m_showGraph);
        ImGui::MenuItem("Tools",           nullptr, &m_showTools);
        ImGui::MenuItem("Tool Properties", nullptr, &m_showToolProperties);
        ImGui::MenuItem("Info",            nullptr, &m_showInfo);
        ImGui::Separator();
        ImGui::MenuItem("ImGui demo",  nullptr, &m_showDemo);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset layout")) {
            m_resetLayout = true;
            m_showCanvas = m_showGraph = m_showTools = m_showToolProperties = m_showInfo = true;
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void UserInterface::drawDockspace()
{
    static const ImGuiID kDockId = ImHashStr("MyAppMainDockspace.v2");   // bumped when the default layout changes
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    if (m_resetLayout || ImGui::DockBuilderGetNode(kDockId) == nullptr) {
        buildDefaultLayout(kDockId, vp->WorkSize.x, vp->WorkSize.y);
        m_resetLayout = false;
    }
    ImGui::DockSpaceOverViewport(kDockId, vp);
}

void UserInterface::buildDefaultLayout(unsigned int id, float w, float h)
{
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(id, ImVec2(w, h));

    // Tools strip | canvas over graph | tool properties over info, Photoshop-like.
    ImGuiID center = id;
    const float toolsW = ImGui::GetFontSize() * 3.4f;
    const ImGuiID tools = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, toolsW / w, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
    const ImGuiID info  = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.30f, nullptr, &right);
    const ImGuiID graph = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.48f, nullptr, &center);
    ImGui::DockBuilderSetNodeSize(tools, ImVec2(toolsW, h));

    ImGui::DockBuilderDockWindow("Tools",           tools);
    ImGui::DockBuilderDockWindow("Tool Properties", right);
    ImGui::DockBuilderDockWindow("Info",            info);
    ImGui::DockBuilderDockWindow("Canvas",          center);
    ImGui::DockBuilderDockWindow("Graph",           graph);
    ImGui::DockBuilderFinish(id);
}

void UserInterface::handleShortcuts()
{
    m_tools.handleShortcuts(*m_project);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) m_importer.showOpenDialog(m_window);
    // Global: they work whichever panel has focus (text fields keep their own Ctrl+Z).
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) m_project->undo();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal))
        m_project->redo();
}

void UserInterface::drawInfoPanel()
{
    if (ImGui::Begin("Info", &m_showInfo)) {
        ImGui::TextWrapped("%s", m_gpuInfo.c_str());
        ImGui::Separator();
        ImGui::Text("acquire (vsync/WSI): %.2f ms", m_acquireMs);
        ImGui::Text("CPU work:            %.2f ms", m_cpuMs);
        ImGui::Text("%.1f FPS (%.3f ms)", ImGui::GetIO().Framerate,
                    1000.0f / ImGui::GetIO().Framerate);
        ImGui::Separator();
        for (int c = 0; c < Perf::Count; ++c)   // ms per frame: smoothed, worst of the last second
            ImGui::Text("%-12s %6.2f  max %6.2f", Perf::name(Perf::Counter(c)), Perf::average(Perf::Counter(c)), Perf::worst(Perf::Counter(c)));
    }
    ImGui::End();
}
