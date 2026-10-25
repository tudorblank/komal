#include "ui-main.h"
#include "core/input.h"
#include "core/perf.h"
#include "core/project.h"
#include "gfx/gpu-context.h"

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder*, FindHoveredWindowEx
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

bool UserInterface::init(GpuContext& gpu, Project& project, const InputFrame& input)
{
    m_input = &input;
    m_project = &project;
    m_graphPanel.init(gpu, project, input);
    const bool ok = m_canvasPanel.init(gpu, project, m_tools, input);
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
    for (const ImageImporter::Imported& i : m_importer.poll(*m_project))
        if (i.placement.target == ImageImporter::Placement::Target::Graph) m_graphPanel.selectOnly(i.node);
    handleShortcuts();
    drawMenuBar();
    drawDockspace();   // must come before the panels that dock into it

    if (m_showTools)          m_toolbox.draw(&m_showTools, m_tools, *m_project);
    if (m_showToolProperties) m_toolProperties.draw(&m_showToolProperties, m_tools, *m_project);
    if (m_showCanvas)         m_canvasPanel.draw(&m_showCanvas);
    if (m_showGraph) { PerfScope perf(Perf::Graph); m_graphPanel.draw(&m_showGraph); }
    if (m_showInfo)           drawInfoPanel();
    if (m_showDemo)           ImGui::ShowDemoWindow(&m_showDemo);
    dispatchDrops();   // after the panels: their windows and rects are this frame's
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

namespace {

using DockShares = absl::flat_hash_map<unsigned int, float>;

// Panels that keep their size when the window is resized (an icon strip doesn't grow).
bool fixedSize(const ImGuiDockNode* node)
{
    if (!node->IsLeafNode() || node->Windows.Size == 0) return false;
    for (const ImGuiWindow* w : node->Windows)
        if (std::strcmp(w->Name, "Tools") != 0) return false;
    return true;
}

// The share of each split its first child has, remembered between resizes: scaling from the
// last frame's sizes instead would round every frame of a window drag, and the layout would
// creep. Only a splitter moved by the user (sizes off by more than a pixel) updates it.
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

// Lays the dock tree under `node` out again for `size`, every split keeping its share (but
// fixed-size panels). ImGui would give the whole difference to one side of each split instead
// (the central node, or whichever its size bookkeeping picks), so a resize lopsided the layout.
// Writes the children's SizeRef, which the dockspace lays out from this frame.
void scaleDockTree(ImGuiDockNode* node, ImVec2 size, const DockShares& shares)
{
    if (node->IsLeafNode()) return;
    ImGuiDockNode* c0 = node->ChildNodes[0];
    ImGuiDockNode* c1 = node->ChildNodes[1];
    const int axis = node->SplitAxis, across = axis ^ 1;
    ImVec2 s0 = size, s1 = size;
    if (c0->IsVisible && c1->IsVisible && c0->Size[axis] > 0.0f && c1->Size[axis] > 0.0f) {
        const float avail = std::max(size[axis] - ImGui::GetStyle().DockingSeparatorSize, 2.0f);
        if (fixedSize(c0) != fixedSize(c1)) {   // the fixed one keeps its size, the other takes the rest
            ImGuiDockNode* fixed = fixedSize(c0) ? c0 : c1;
            const float f = std::min(fixed->Size[axis], avail - 1.0f);
            (fixed == c0 ? s0 : s1)[axis] = f;
            (fixed == c0 ? s1 : s0)[axis] = avail - f;
        } else {
            const auto it = shares.find(node->ID);
            const float share = it != shares.end() ? it->second : childShare(c0, c1, axis);
            s0[axis] = std::max(1.0f, std::round(share * avail));
            s1[axis] = std::max(1.0f, avail - s0[axis]);
        }
        c0->SizeRef[axis] = s0[axis];
        c1->SizeRef[axis] = s1[axis];
    }
    s0[across] = s1[across] = size[across];
    scaleDockTree(c0, s0, shares);
    scaleDockTree(c1, s1, shares);
}

} // namespace

void UserInterface::drawDockspace()
{
    static const ImGuiID kDockId = ImHashStr("KomalMainDockspace.v2");   // bumped when the default layout changes
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    if (m_resetLayout || ImGui::DockBuilderGetNode(kDockId) == nullptr) {
        buildDefaultLayout(kDockId, vp->WorkSize.x, vp->WorkSize.y);
        m_resetLayout = false;
        m_dockShares.clear();
    }
    // The window was resized (maximized, fullscreen, dragged...): the layout scales with it.
    // While its size holds, the shares follow whatever the user does with the splitters.
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

void UserInterface::dispatchDrops()
{
    using Target = ImageImporter::Placement::Target;
    for (Drop& d : m_drops) {
        ImGuiWindow* top = nullptr;
        ImGuiWindow* underMoving = nullptr;
        ImGui::FindHoveredWindowEx(d.pos, false, &top, &underMoving);   // the window in front at that point
        Vec2 world;
        ImVec2 graph;
        if (m_canvasPanel.dropTarget(top, d.pos, world))
            m_importer.open(std::move(d.path), { Target::Canvas, world.x, world.y });
        else if (m_graphPanel.dropTarget(top, d.pos, graph))
            m_importer.open(std::move(d.path), { Target::Graph, graph.x, graph.y });
        else
            std::fprintf(stderr, "drop ignored (not on the canvas or the graph): %s\n", d.path.c_str());
    }
    m_drops.clear();
}

void UserInterface::handleShortcuts()
{
    // Keys come from SDL (InputFrame); ImGui only says whether a text field is typing, which
    // then keeps every key (Ctrl+Z in a name field is the field's).
    const InputFrame& in = *m_input;
    const bool typing = ImGui::GetIO().WantTextInput;
    m_tools.handleShortcuts(*m_project, in, typing);
    if (typing) return;
    // Global: they work whichever panel has focus. Primary is Cmd on macOS, Ctrl elsewhere.
    if (in.pressed({ SDLK_O, ModPrimary })) m_importer.showOpenDialog(m_window);
    if (in.pressed({ SDLK_Z, ModPrimary }, true)) m_project->undo();   // held: keeps undoing
    if (in.pressed({ SDLK_Z, ModPrimary | ModShift }, true) || in.pressed({ SDLK_Y, ModPrimary }, true))
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
        if (m_input && m_input->pen.present) {
            const InputFrame::Pen& pen = m_input->pen;
            ImGui::Text("pen: %s %s  pressure %.2f  tilt %.0f %.0f", pen.eraser ? "eraser" : "tip",
                        pen.touching ? "down" : "up", pen.pressure, pen.tiltX, pen.tiltY);
        }
        ImGui::Separator();
        for (int c = 0; c < Perf::Count; ++c)   // ms per frame: smoothed, worst of the last second
            ImGui::Text("%-12s %6.2f  max %6.2f", Perf::name(Perf::Counter(c)), Perf::average(Perf::Counter(c)), Perf::worst(Perf::Counter(c)));
    }
    ImGui::End();
}
