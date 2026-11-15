#include "ui/ui-main.hpp"
#include "base/file.hpp"
#include "base/log.hpp"
#include "input/input-frame.hpp"
#include "project/project-actions.hpp"
#include "project/projects-manager-actions.hpp"
#include "project/workspace.hpp"
#include "ui/panels/canvas/canvas-panel.hpp"
#include "ui/panels/project-tabs-panel.hpp"
#include "ui/panels/graph/graph-panel.hpp"
#include "ui/panels/history-panel.hpp"
#include "ui/panels/info-panel.hpp"
#include "ui/panels/intro-screen.hpp"
#include "ui/keymap.hpp"
#include "ui/panels/navigator-panel.hpp"
#include "ui/panels/color-panel.hpp"
#include "ui/panels/tool-properties-panel.hpp"
#include "ui/panels/toolbox-panel.hpp"
#include "ui/style/ui-style.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <algorithm>

UserInterface::UserInterface()
{
    // Every panel, in View menu order. Where each docks and when it draws are its own (PanelInfo).
    const CanvasPanel* canvas = addPanel<CanvasPanel>();
    const GraphPanel*  graph  = addPanel<GraphPanel>();
    m_intake.init(*canvas, *graph);   // the ones a drop can land on
    addPanel<ToolboxPanel>();
    addPanel<ProjectTabsPanel>();   // under the Toolbox: bars stack in this order
    addPanel<NavigatorPanel>();
    addPanel<ColorPanel>();
    addPanel<ToolPropertiesPanel>();
    addPanel<HistoryPanel>();
    addPanel<InfoPanel>();

    for (const auto& p : m_panels) m_drawOrder.push_back(p.get());
    std::stable_partition(m_drawOrder.begin(), m_drawOrder.end(), [](const Panel* p) { return p->m_info.phase == DrawPhase::Edit; });
    m_dockspace.follow(m_panels);
}

template<class T>
T* UserInterface::addPanel()
{
    auto panel = std::make_unique<T>();
    T* added = panel.get();
    Panel& base = *added;
    base.m_info = added->info();
    base.m_open = base.m_info.openByDefault;
    m_panels.push_back(std::move(panel));
    return added;
}

bool UserInterface::init(GpuDevice& gpu, ProjectsManager& projects, const InputFrame& input, const HostInfo& host)
{
    m_input = &input;
    m_host  = &host;
    m_projects = &projects;
    // What a user can do to the app and the open projects; the active project's own come
    // through it. Panels add their own interface actions (framing, selecting, opening menus) as they init.
    m_unsaved.init(projects, m_actions, input);
    registerAppActions();
    registerProjectsManagerActions(m_projectActions, projects);
    registerProjectRequests();
    bool ok = m_canvas.init(gpu);
    m_intro.init(gpu);
    for (const auto& d : projects.all()) m_canvas.open(d->id(), d->workspace());
    m_projectsEvents = projects.changed.connect([this](const ProjectsEvent& e) { projectsChanged(e); });
    m_context.emplace(UiContext{ gpu, projects, m_actions, m_tools, input, m_owner, m_canvas, host });
    for (const auto& p : m_panels) {
        p->m_ui = &*m_context;
        ok = p->init() && ok;   // one failing leaves the rest working
    }
    // Every action is there now; a project's through a stand-in, so their keys are bound with none open.
    {
        Project keys;
        m_actions.delegateTo(&keys.actions());
        bindDefaultKeys(m_actions);
        m_actions.delegateTo(nullptr);   // before the stand-in goes
    }
    m_actions.delegateTo(projects.empty() ? nullptr : &projects.active().actions());
    m_canvas.activate(projects.activeId());
    m_initialized = true;
    return ok;
}

void UserInterface::projectsChanged(const ProjectsEvent& e)
{
    switch (e.type) {
    case ProjectsEvent::Type::Opened:
        m_canvas.open(e.id, m_projects->find(e.id)->workspace());
        for (const auto& p : m_panels) p->projectsChanged(e);
        break;
    case ProjectsEvent::Type::Closing:   // it's still there: everything lets go of it, the engine last
        if (e.id == m_projects->active().id()) m_actions.delegateTo(nullptr);
        for (auto p = m_panels.rbegin(); p != m_panels.rend(); ++p) (*p)->projectsChanged(e);
        m_canvas.close(e.id);
        m_unsaved.closing(e.id);
        break;
    case ProjectsEvent::Type::Activated:
        m_actions.delegateTo(&m_projects->find(e.id)->actions());
        m_canvas.activate(e.id);
        for (const auto& p : m_panels) p->projectsChanged(e);
        break;
    }
}

void UserInterface::registerProjectRequests()
{
    for (const Action& a : m_projectActions.actions()) {
        const ActionId id = a.id;
        m_actions.add({ .id = id, .title = a.title, .kind = ActionKind::Interface, .args = a.args,
                        .enabled = [this, id] { return m_projectActions.find(id)->isEnabled(); },
                        .run = [this, id](const ActionArgs& args) {
            m_projectRequests.push_back({ id, args });
            return ActionResult::later();   // applyProjectRequests runs it and reports how it went
        } });
    }
}

void UserInterface::applyProjectRequests()
{
    std::vector<std::pair<ActionId, ActionArgs>> requests = std::move(m_projectRequests);
    m_projectRequests.clear();
    for (auto& [id, args] : requests) {
        const Project* target = projectOf(args);
        const ProjectId project = target ? target->id() : kNoProject;
        const std::string file = File::name(args.get(ProjectArgs::kPath, std::string()));
        const ActionResult r = m_projectActions.run(id, std::move(args));
        if (id == Actions::ProjectSave) m_unsaved.saved(project, bool(r));   // a question's "Yes", maybe
        if (r || r.error.empty()) continue;
        if (id == Actions::ProjectOpen) m_notices.post("Couldn't open " + file + ": " + r.error);
        if (id == Actions::ProjectSave) m_notices.post("Couldn't save: " + r.error);
    }
}

void UserInterface::requestQuit()
{
    m_actions.run(Actions::AppQuit);
}

void UserInterface::shutdown()
{
    if (!m_initialized) return;
    m_projectsEvents.disconnect();
    for (auto p = m_panels.rbegin(); p != m_panels.rend(); ++p) (*p)->shutdown();
    m_canvas.shutdown();   // after every view of it
    m_actions.delegateTo(nullptr);
    m_initialized = false;
}


bool UserInterface::drawn(const Panel& p) const
{
    return p.m_info.slot == DockSlot::Toolbar || !m_projects->empty();
}

void UserInterface::releaseGpu()
{
    for (auto p = m_panels.rbegin(); p != m_panels.rend(); ++p) (*p)->releaseGpu();
    m_intro.releaseGpu();
    m_canvas.releaseGpu();   // after every view of it
}

bool UserInterface::restoreGpu(GpuDevice& gpu)
{
    const bool ok = m_canvas.restoreGpu(gpu);   // before every view of it
    m_intro.restoreGpu(gpu);
    for (const auto& p : m_panels) p->restoreGpu();
    return ok;
}

Clock::Seconds UserInterface::nextFrameIn() const
{
    // Imports wake the loop when they finish; a stroke changes nothing while the pen rests.
    // A closed panel needs no frames (each knows it's closed).
    Clock::Seconds next = std::min(m_canvas.nextFrameIn(), m_notices.nextFrameIn());
    if (m_projects)
        for (const auto& d : m_projects->all())
            if (!d->workspace().historySettled()) next = std::min(next, kFrameNow);   // until it is
    for (const auto& p : m_panels)
        if (drawn(*p)) next = std::min(next, p->nextFrameIn());
    return next;
}

void UserInterface::renderGpu(WGPUCommandEncoder enc)
{
    m_canvas.beginFrame();   // first: the output's tiles, for every view of them
    for (const auto& p : m_panels)
        if (drawn(*p)) p->renderGpu(enc);
    m_canvas.renderThumbnails(enc);   // last: the least urgent
}

void UserInterface::draw()
{
    for (const ProjectId cancelled : m_intake.takeInbox(m_actions)) m_unsaved.saved(cancelled, false);   // not saved: nothing closes
    applyProjectRequests();   // last frame's (its pictures are shown now): before anything draws
    if (m_projects->empty() && m_intake.importReady()) m_projectActions.run(Actions::ProjectNew);   // an image lands in a new one
    const ImageImporter::Polled polled = m_intake.poll(m_actions);
    for (const ImageImporter::Imported& i : polled.imported)
        if (std::holds_alternative<ImageImporter::InGraph>(i.placement))
            m_actions.run(Actions::SelectionSet, selectionArgs(NodeSelection::only(i.node)));
    for (const std::string& error : polled.failed) m_notices.post("Couldn't import " + error);
    if (m_canvas.outOfMemory() && !m_toldOutOfMemory) m_notices.post("Out of GPU memory: parts of the picture can't be shown");
    m_toldOutOfMemory = m_canvas.outOfMemory();
    m_owner.beginFrame();
    if (!m_owner.typing()) m_actions.dispatch(*m_input, m_owner.keyboard());   // the frame's shortcuts, all of them, before the panels
    applyProjectRequests();   // a key's (Ctrl+N): nothing is drawn yet
    drawMenuBar();

    // Bars under the menu bar, then the dockspace takes the rest (with no project open, the
    // intro does). Then everything else, Edit panels before Show panels (DrawPhase).
    for (Panel* p : m_drawOrder)
        if (p->m_info.slot == DockSlot::Toolbar) p->draw();
    if (m_projects->empty()) m_intro.draw(m_actions);
    else m_dockspace.draw();
    for (Panel* p : m_drawOrder)
        if (p->m_info.slot != DockSlot::Toolbar && drawn(*p)) p->draw();

    m_intake.dispatchDrops(m_actions);   // after the panels: their windows and rects are this frame's
    m_unsaved.draw();
    m_notices.draw();
    // A control held or a key down: a coalescing setting (a slider drag, a held [ ) is still going.
    m_actions.endFrame(ImGui::IsAnyItemActive() || m_input->anyButtonHeld() || !m_input->held.empty());
    settleHistories();
}

void UserInterface::settleHistories()
{
    // The active project first; the others after, on what's left.
    if (m_projects->empty()) return;
    const Deadline until = Deadline::in(kHistoryShare);
    if (!m_projects->active().workspace().settleHistory(until)) return;
    for (const auto& d : m_projects->all())
        if (!d->workspace().settleHistory(until)) return;
}

void UserInterface::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File")) {
        Widgets::actionMenuItem(m_actions, Actions::ProjectNew);
        Widgets::actionMenuItem(m_actions, Actions::FileOpen);
        ImGui::Separator();
        Widgets::actionMenuItem(m_actions, Actions::FileSave);
        Widgets::actionMenuItem(m_actions, Actions::FileSaveAs);
        Widgets::actionMenuItem(m_actions, Actions::FileClose);
        ImGui::Separator();
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
            for (const auto& p : m_panels)
                if ((p->m_info.slot == DockSlot::Floating) == floating) Widgets::actionMenuItem(m_actions, p->toggleAction());
            ImGui::Separator();
        }
        Widgets::actionMenuItem(m_actions, Actions::ViewResetLayout);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}
