#include "ui-main.hpp"
#include "core/input.hpp"
#include "ui/action-ui.hpp"
#include "ui/widgets.hpp"
#include "core/perf.hpp"
#include "core/project.hpp"
#include "gfx/device.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder*, FindHoveredWindowEx
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

bool UserInterface::init(GpuDevice& gpu, Project& project, const InputFrame& input)
{
    m_input = &input;
    m_project = &project;
    registerActions();
    m_tools.registerActions(m_actions, project);
    m_graphPanel.init(gpu, project, input, m_actions);
    const bool ok = m_canvasPanel.init(gpu, project, m_tools, input, m_actions);
    m_navigator.init(gpu, project);
    m_initialized = true;
    return ok;
}

void UserInterface::shutdown()
{
    if (!m_initialized) return;
    m_graphPanel.shutdown();
    m_navigator.shutdown();
    m_canvasPanel.shutdown();
    m_initialized = false;
}

bool UserInterface::busy() const
{
    return m_importer.busy() || !m_drops.empty() || m_project->strokeActive() || m_canvasPanel.busy() || m_graphPanel.busy() ||
           m_navigator.busy();
}

void UserInterface::renderGpu(WGPUCommandEncoder enc)
{
    m_canvasPanel.renderGpu(enc);
}

void UserInterface::draw()
{
    for (const ImageImporter::Imported& i : m_importer.poll(*m_project))
        if (i.placement.target == ImageImporter::Placement::Target::Graph) m_graphPanel.selectOnly(i.node);
    if (!ImGui::GetIO().WantTextInput) m_actions.dispatch(*m_input, ActionScope::Global);   // a typing field keeps its keys
    drawMenuBar();
    if (m_showTools) m_toolbox.draw(m_tools, *m_project, m_actions);   // under the menu bar, before the dockspace takes the rest
    drawDockspace();   // must come before the panels that dock into it

    if (m_showColor)          m_colorPanel.draw(&m_showColor, m_tools.colors());
    if (m_showToolProperties) m_toolProperties.draw(&m_showToolProperties, m_tools, *m_project);
    // What edits the project (graph, navigator) runs before the canvas, so the canvas draws this
    // frame's state: its outlines and its GPU picture always agree.
    m_graphPanel.setPointerElsewhere(m_showCanvas && m_canvasPanel.hasPointer());   // last frame's
    if (m_showGraph) { PerfScope perf(Perf::Graph); m_graphPanel.draw(&m_showGraph); }
    if (BoundsI b; m_graphPanel.takeCanvasFrame(b)) m_canvasPanel.frameWorld(b);   // a node double-clicked: go to its pixels
    if (m_showNavigator)      m_navigator.draw(&m_showNavigator, m_tools.canvas());
    m_canvasPanel.setSelection({ m_graphPanel.selection().begin(), m_graphPanel.selection().end() });
    if (m_showCanvas)         m_canvasPanel.draw(&m_showCanvas);
    if (m_showInfo)           drawInfoPanel();
    if (m_showDemo)           ImGui::ShowDemoWindow(&m_showDemo);
    dispatchDrops();   // after the panels: their windows and rects are this frame's
}

void UserInterface::registerActions()
{
    // Primary is Cmd on macOS, Ctrl elsewhere.
    m_actions.add({ .id = "file.import", .title = "Import Image...", .shortcuts = { { SDLK_O, ModPrimary } },
                    .run = [this] { m_importer.showOpenDialog(m_window); } });
    m_actions.add({ .id = "app.quit", .title = "Quit", .run = [this] { m_quit = true; } });

    m_actions.add({ .id = "edit.undo", .title = "Undo", .shortcuts = { { SDLK_Z, ModPrimary } }, .repeats = true,
                    .enabled = [this] { return m_project->canUndo(); }, .run = [this] { m_project->undo(); } });
    m_actions.add({ .id = "edit.redo", .title = "Redo", .shortcuts = { { SDLK_Z, ModPrimary | ModShift }, { SDLK_Y, ModPrimary } },
                    .repeats = true, .enabled = [this] { return m_project->canRedo(); }, .run = [this] { m_project->redo(); } });

    const auto panel = [this](const char* id, const char* title, bool& shown) {
        m_actions.add({ .id = id, .title = title, .checked = [&shown] { return shown; }, .run = [&shown] { shown = !shown; } });
    };
    panel("view.canvas",          "Canvas",          m_showCanvas);
    panel("view.graph",           "Graph",           m_showGraph);
    panel("view.tools",           "Toolbar",         m_showTools);
    panel("view.navigator",       "Navigator",       m_showNavigator);
    panel("view.color",           "Color",           m_showColor);
    panel("view.toolProperties",  "Tool Properties", m_showToolProperties);
    panel("view.info",            "Info",            m_showInfo);
    panel("view.imguiDemo",       "ImGui demo",      m_showDemo);
    m_actions.add({ .id = "view.resetLayout", .title = "Reset layout", .run = [this] {
        m_resetLayout = true;
        m_showCanvas = m_showGraph = m_showTools = m_showToolProperties = m_showColor = m_showNavigator = true;
        m_showInfo = false;   // on demand (View > Info)
    } });
}

void UserInterface::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File")) {
        actionMenuItem(m_actions, "file.import");
        ImGui::Separator();
        actionMenuItem(m_actions, "app.quit");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        actionMenuItem(m_actions, "edit.undo");
        actionMenuItem(m_actions, "edit.redo");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        for (const char* id : { "view.canvas", "view.graph", "view.tools", "view.navigator", "view.color", "view.toolProperties", "view.info" })
            actionMenuItem(m_actions, id);
        ImGui::Separator();
        actionMenuItem(m_actions, "view.imguiDemo");
        ImGui::Separator();
        actionMenuItem(m_actions, "view.resetLayout");
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

namespace {

using DockShares = absl::flat_hash_map<unsigned int, float>;

// Each split's first-child share, remembered (scaling from last frame's sizes would creep from
// rounding). Only a splitter the user moved (off by over a pixel) updates it.
float childShare(const ImGuiDockNode* c0, const ImGuiDockNode* c1, int axis)
{
    return c0->Size[axis] / (c0->Size[axis] + c1->Size[axis]);
}

void rememberShares(const ImGuiDockNode* node, DockShares& shares)
{
    if (node->IsLeafNode()) return;
    const ImGuiDockNode* c0 = node->ChildNodes[0];
    const ImGuiDockNode* c1 = node->ChildNodes[1];
    const int axis = node->SplitAxis;
    if (c0->IsVisible && c1->IsVisible && c0->Size[axis] > 0.0f && c1->Size[axis] > 0.0f) {
        const float total = c0->Size[axis] + c1->Size[axis];
        auto [it, inserted] = shares.try_emplace(node->ID, childShare(c0, c1, axis));
        if (!inserted && std::fabs(it->second * total - c0->Size[axis]) > 1.0f) it->second = childShare(c0, c1, axis);
    }
    rememberShares(c0, shares);
    rememberShares(c1, shares);
}

// Whether a dock area grows with the window: it holds the canvas or the graph. Others keep their size.
bool flexible(const ImGuiDockNode* node)
{
    if (!node) return false;
    if (!node->IsLeafNode()) return flexible(node->ChildNodes[0]) || flexible(node->ChildNodes[1]);
    for (const ImGuiWindow* w : node->Windows)
        if (std::strcmp(w->Name, "Canvas") == 0 || std::strcmp(w->Name, "Graph") == 0) return true;
    return false;
}

// Re-lays the dock tree under `node` for `size` (ImGui alone gives the whole difference to one
// side). At each split:
//   flexible | fixed     the fixed side keeps its size (the side column keeps its width)
//   otherwise            each keeps its share (stacked panels grow together)
// Writes the children's SizeRef, which the dockspace uses this frame.
void scaleDockTree(ImGuiDockNode* node, ImVec2 size, const DockShares& shares)
{
    if (node->IsLeafNode()) return;
    ImGuiDockNode* c0 = node->ChildNodes[0];
    ImGuiDockNode* c1 = node->ChildNodes[1];
    const int axis = node->SplitAxis, across = axis ^ 1;
    ImVec2 s0 = size, s1 = size;
    if (c0->IsVisible && c1->IsVisible && c0->Size[axis] > 0.0f && c1->Size[axis] > 0.0f) {
        const float avail = std::max(size[axis] - ImGui::GetStyle().DockingSeparatorSize, 2.0f);
        const bool flex0 = flexible(c0), flex1 = flexible(c1);
        if (flex0 == flex1) {
            const auto it = shares.find(node->ID);
            const float share = it != shares.end() ? it->second : childShare(c0, c1, axis);
            s0[axis] = std::max(1.0f, std::round(share * avail));
            s1[axis] = std::max(1.0f, avail - s0[axis]);
        } else {
            ImGuiDockNode* kept = flex0 ? c1 : c0;   // the fixed side keeps its size
            const float k = std::clamp(kept->Size[axis], 1.0f, avail - 1.0f);
            (kept == c0 ? s0 : s1)[axis] = k;
            (kept == c0 ? s1 : s0)[axis] = avail - k;
        }
        c0->SizeRef[axis] = s0[axis];
        c1->SizeRef[axis] = s1[axis];
    }
    s0[across] = s1[across] = size[across];
    scaleDockTree(c0, s0, shares);
    scaleDockTree(c1, s1, shares);
}

}

void UserInterface::drawDockspace()
{
    static const ImGuiID kDockId = ImHashStr("KomalMainDockspace.v6");   // bumped when the default layout changes
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    if (m_resetLayout || ImGui::DockBuilderGetNode(kDockId) == nullptr) {
        buildDefaultLayout(kDockId, vp->WorkSize.x, vp->WorkSize.y);
        m_resetLayout = false;
        m_dockShares.clear();
    }
    // The window resized: the layout scales with it. Otherwise the shares follow the splitters.
    if (ImGuiDockNode* root = ImGui::DockBuilderGetNode(kDockId); root && root->Size.x > 0.0f && root->Size.y > 0.0f) {
        if (root->Size.x != vp->WorkSize.x || root->Size.y != vp->WorkSize.y) scaleDockTree(root, vp->WorkSize, m_dockShares);
        else                                                                   rememberShares(root, m_dockShares);
    }
    ImGui::DockSpaceOverViewport(kDockId, vp);
}

void UserInterface::buildDefaultLayout(unsigned int id, float w, float h)
{
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(id, ImVec2(w, h));

    // Canvas over graph | navigator over colour over tool properties over info (the toolbar is
    // above, not docked).
    ImGuiID center = id;
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
    const ImGuiID navigator = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.32f, nullptr, &right);
    const ImGuiID color = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.48f, nullptr, &right);
    const ImGuiID info  = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.30f, nullptr, &right);
    const ImGuiID graph = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.48f, nullptr, &center);

    ImGui::DockBuilderDockWindow("Navigator",       navigator);
    ImGui::DockBuilderDockWindow("Color",           color);
    ImGui::DockBuilderDockWindow("Tool Properties", right);
    ImGui::DockBuilderDockWindow("Info",            info);
    ImGui::DockBuilderDockWindow("Canvas",          center);
    ImGui::DockBuilderDockWindow("Graph",           graph);
    ImGui::DockBuilderFinish(id);
}

void UserInterface::dispatchDrops()
{
    using Target = ImageImporter::Placement::Target;
    for (Drop& d : m_drops) {
        ImGuiWindow* top = nullptr;
        ImGuiWindow* underMoving = nullptr;
        ImGui::FindHoveredWindowEx(d.pos, false, &top, &underMoving);   // the window in front at that point
        Vec2 world;
        ImVec2 nodePos;
        if (m_canvasPanel.dropTarget(top, d.pos, world))
            m_importer.open(std::move(d.path), { Target::Canvas, world.x, world.y });
        else if (m_graphPanel.dropTarget(top, d.pos, nodePos))
            m_importer.open(std::move(d.path), { Target::Graph, nodePos.x, nodePos.y });
        else
            std::fprintf(stderr, "drop ignored (not on the canvas or the graph): %s\n", d.path.c_str());
    }
    m_drops.clear();
}


void UserInterface::drawInfoPanel()
{
    if (Widgets::beginPanel("Info", &m_showInfo)) {
        ImGui::TextWrapped("%s", m_gpuInfo.c_str());
        ImGui::Separator();
        ImGui::Text("acquire (vsync/WSI): %.2f ms", m_acquireMs);
        ImGui::Text("CPU work:            %.2f ms", m_cpuMs);
        ImGui::Text("%.1f FPS (%.3f ms)", ImGui::GetIO().Framerate,
                    1000.0f / ImGui::GetIO().Framerate);
        if (m_input && m_input->pen.present) {
            const InputFrame::Pen& pen = m_input->pen;
            ImGui::Text("pen: %s %s  pressure %.2f  tilt %.0f %.0f", pen.eraser ? "eraser" : "tip",
                        pen.touching ? "down" : "up", pen.pressure, pen.tiltX, pen.tiltY);
        }
        ImGui::Separator();
        for (int c = 0; c < Perf::Count; ++c)   // ms per frame: smoothed, worst of the last second
            ImGui::Text("%-12s %6.2f  max %6.2f", Perf::name(Perf::Counter(c)), Perf::average(Perf::Counter(c)), Perf::worst(Perf::Counter(c)));
    }
    Widgets::endPanel();
}
