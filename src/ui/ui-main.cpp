#include "ui-main.hpp"
#include "core/application.hpp"
#include "core/document-actions.hpp"
#include "core/input.hpp"
#include "ui/widgets.hpp"
#include "ui/panel-titles.hpp"
#include "core/perf.hpp"
#include "core/project.hpp"
#include "gfx/device.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // FindHoveredWindowEx
#include <algorithm>
#include <cstdio>

bool UserInterface::init(GpuDevice& gpu, Project& project, const InputFrame& input)
{
    m_input = &input;
    m_project = &project;
    // The two lists of what a user can do: to the document, and to the app. Panels add their own
    // interface actions (framing, selecting, opening menus) as they init.
    registerDocumentActions(m_actions, project);
    registerAppActions();
    const bool ok = m_canvasPanel.init(gpu, project, m_tools, input, m_actions);
    // The canvas's pipelines draw the navigator's picture and the graph's thumbnails too.
    m_graphPanel.init(ok ? &m_canvasPanel.pipelines() : nullptr, project, input, m_actions);
    if (ok) m_navigator.init(project, m_canvasPanel.outputTiles(), m_canvasPanel.pipelines());

    // The canvas follows the graph: it outlines what's selected there, and goes where asked.
    m_graphPanel.onSelectionChanged([this](const GraphSelection::Set& ids) { m_canvasPanel.setSelection({ ids.begin(), ids.end() }); });
    m_graphPanel.onShowInCanvas([this](const BoundsI& b) { m_canvasPanel.frameWorld(b); });
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

double UserInterface::nextFrameIn() const
{
    // Imports wake the loop when they finish; a stroke changes nothing while the pen rests.
    // A closed panel needs no frames (each knows it's closed).
    if (m_canvasPanel.busy()) return 0.0;
    return std::min(m_graphPanel.nextFrameIn(), m_navigator.nextFrameIn());
}

void UserInterface::renderGpu(WGPUCommandEncoder enc)
{
    m_canvasPanel.renderGpu(enc);   // first: syncs the output's tiles
    m_navigator.renderGpu(enc);
    m_graphPanel.renderGpu(enc);
}

void UserInterface::draw()
{
    for (const ImageImporter::Imported& i : m_importer.poll(m_actions))
        if (i.placement.target == ImageImporter::Placement::Target::Graph) m_graphPanel.selectOnly(i.node);
    m_actions.dispatch(*m_input, ImGui::GetIO().WantTextInput);   // the frame's shortcuts, all of them, before the panels
    drawMenuBar();
    m_toolbox.draw(m_tools, m_actions);   // under the menu bar, before the dockspace takes the rest
    m_dockspace.draw();   // before the panels that dock into it

    // The canvas view as it was last drawn, for what navigates it from outside.
    const CanvasNavigation canvas = m_canvasPanel.navigation();
    m_colorPanel.draw(m_tools.colors(), m_actions);
    m_toolProperties.draw(m_tools, *m_project, m_actions, canvas);
    m_historyPanel.draw(m_project->history(), m_actions);
    // What edits the project or the view (graph, navigator) runs before the canvas, so the canvas
    // draws this frame's state: its outlines and its GPU picture always agree.
    { PerfScope perf(Perf::Graph); m_graphPanel.draw(); }
    m_navigator.draw(canvas, m_actions);
    m_canvasPanel.draw();
    m_info.draw(*m_input, m_actions);
    if (m_showDemo) {
        bool keep = true;
        ImGui::ShowDemoWindow(&keep);
        if (!keep) m_actions.run("view.imguiDemo");
    }
    dispatchDrops();   // after the panels: their windows and rects are this frame's
    // A control held or a key down: a coalescing setting (a slider drag, a held [ ) is still going.
    m_actions.endFrame(ImGui::IsAnyItemActive() || m_input->anyButtonHeld() || !m_input->held.empty());
}

void UserInterface::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File")) {
        Widgets::actionMenuItem(m_actions, "file.import");
        ImGui::Separator();
        Widgets::actionMenuItem(m_actions, "app.quit");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        Widgets::actionMenuItem(m_actions, "edit.undo");
        Widgets::actionMenuItem(m_actions, "edit.redo");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        for (const char* id : { "view.canvas", "view.graph", "view.tools", "view.navigator", "view.color", "view.toolProperties",
                                "view.historyTree", "view.info" })
            Widgets::actionMenuItem(m_actions, id);
        ImGui::Separator();
        Widgets::actionMenuItem(m_actions, "view.imguiDemo");
        ImGui::Separator();
        Widgets::actionMenuItem(m_actions, "view.resetLayout");
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
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
