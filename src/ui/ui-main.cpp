#include "ui-main.hpp"
#include "base/log.hpp"
#include "command/input.hpp"
#include "document/document-actions.hpp"
#include "document/project.hpp"
#include "ui/canvas-panel.hpp"
#include "ui/demo-panel.hpp"
#include "ui/graph-panel/panel.hpp"
#include "ui/history-panel.hpp"
#include "ui/info-panel.hpp"
#include "ui/navigator.hpp"
#include "ui/tools/color-panel.hpp"
#include "ui/tools/tool-properties-panel.hpp"
#include "ui/tools/toolbox-panel.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // FindHoveredWindowEx
#include <algorithm>

UserInterface::UserInterface()
{
    // Every panel, in View menu order. Where each docks and when it draws are its own (PanelInfo).
    m_canvasPanel = addPanel<CanvasPanel>();
    m_graphPanel  = addPanel<GraphPanel>();
    addPanel<ToolboxPanel>();
    addPanel<Navigator>();
    addPanel<ColorPanel>();
    addPanel<ToolPropertiesPanel>();
    addPanel<HistoryPanel>();
    addPanel<InfoPanel>();
    addPanel<DemoPanel>();

    for (PanelEntry& e : m_panels) m_drawOrder.push_back(&e);
    std::stable_partition(m_drawOrder.begin(), m_drawOrder.end(), [](const PanelEntry* e) { return e->info.phase == DrawPhase::Edit; });
}

template<class T>
T* UserInterface::addPanel()
{
    auto panel = std::make_unique<T>();
    T* added = panel.get();
    const PanelEntry& e = m_panels.emplace_back(PanelEntry{ std::move(panel), added->info() });
    Panel& base = *added;
    base.m_open = e.info.openByDefault;
    base.m_title  = e.info.title;
    base.m_toggle = e.info.toggle;
    m_dockspace.addPanel(e.info);
    return added;
}

bool UserInterface::init(GpuDevice& gpu, Project& project, const InputFrame& input, const HostInfo& host)
{
    m_input = &input;
    m_host  = &host;
    // The two lists of what a user can do: to the document, and to the app. Panels add their own
    // interface actions (framing, selecting, opening menus) as they init.
    registerDocumentActions(m_actions, project);
    registerAppActions();
    bool ok = m_canvas.init(gpu, project);
    m_context.emplace(UiContext{ gpu, project, m_actions, m_tools, input, m_canvas, m_canvas.thumbnails(), host });
    for (PanelEntry& e : m_panels) {
        e.panel->m_ui = &*m_context;
        ok = e.panel->init() && ok;   // one failing leaves the rest working
    }
    m_initialized = true;
    return ok;
}

void UserInterface::shutdown()
{
    if (!m_initialized) return;
    for (auto e = m_panels.rbegin(); e != m_panels.rend(); ++e) e->panel->shutdown();
    m_canvas.shutdown();   // after every view of it
    m_initialized = false;
}


double UserInterface::nextFrameIn() const
{
    // Imports wake the loop when they finish; a stroke changes nothing while the pen rests.
    // A closed panel needs no frames (each knows it's closed).
    double next = std::min(m_canvas.nextFrameIn(), m_notices.nextFrameIn());
    for (const PanelEntry& e : m_panels) next = std::min(next, e.panel->nextFrameIn());
    return next;
}

void UserInterface::renderGpu(WGPUCommandEncoder enc)
{
    m_canvas.beginFrame();   // first: the output's tiles, for every view of them
    for (const PanelEntry& e : m_panels) e.panel->renderGpu(enc);
    m_canvas.renderThumbnails(enc);   // last: the least urgent
}

void UserInterface::draw()
{
    const ImageImporter::Polled polled = m_importer.poll(m_actions);
    for (const ImageImporter::Imported& i : polled.imported)
        if (std::holds_alternative<ImageImporter::InGraph>(i.placement))
            m_actions.run(Actions::SelectionSet, selectionArgs(NodeSelection::only(i.node)));
    for (const std::string& why : polled.failed) m_notices.post("Couldn't import " + why);
    m_actions.dispatch(*m_input, ImGui::GetIO().WantTextInput);   // the frame's shortcuts, all of them, before the panels
    drawMenuBar();

    // Bars under the menu bar, then the dockspace takes the rest. Then everything else, Edit
    // panels before Show panels (DrawPhase).
    for (PanelEntry* e : m_drawOrder)
        if (e->info.slot == DockSlot::Toolbar) e->panel->draw();
    m_dockspace.draw();
    for (PanelEntry* e : m_drawOrder)
        if (e->info.slot != DockSlot::Toolbar) e->panel->draw();

    dispatchDrops();   // after the panels: their windows and rects are this frame's
    m_notices.draw();
    // A control held or a key down: a coalescing setting (a slider drag, a held [ ) is still going.
    m_actions.endFrame(ImGui::IsAnyItemActive() || m_input->anyButtonHeld() || !m_input->held.empty());
}

void UserInterface::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File")) {
        Widgets::actionMenuItem(m_actions, Actions::FileImport);
        ImGui::Separator();
        Widgets::actionMenuItem(m_actions, Actions::AppQuit);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        Widgets::actionMenuItem(m_actions, Actions::EditUndo);
        Widgets::actionMenuItem(m_actions, Actions::EditRedo);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        for (const bool floating : { false, true }) {   // the docked panels, then the floating ones
            for (const PanelEntry& e : m_panels)
                if ((e.info.slot == DockSlot::Floating) == floating) Widgets::actionMenuItem(m_actions, e.panel->toggleAction());
            ImGui::Separator();
        }
        Widgets::actionMenuItem(m_actions, Actions::ViewResetLayout);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void UserInterface::dispatchDrops()
{
    for (Drop& d : m_drops) {
        ImGuiWindow* top = nullptr;
        ImGuiWindow* underMoving = nullptr;
        ImGui::FindHoveredWindowEx(d.pos, false, &top, &underMoving);   // the window in front at that point
        Vec2 world;
        ImVec2 nodePos;
        if (m_canvasPanel->dropTarget(top, d.pos, world))
            m_importer.open(std::move(d.path), ImageImporter::OnCanvas{ world.x, world.y });
        else if (m_graphPanel->dropTarget(top, d.pos, nodePos))
            m_importer.open(std::move(d.path), ImageImporter::InGraph{ nodePos.x, nodePos.y });
        else
            Log::info("drop", "ignored (not on the canvas or the graph): %s", d.path.c_str());
    }
    m_drops.clear();
}
