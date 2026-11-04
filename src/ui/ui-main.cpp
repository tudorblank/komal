#include "ui-main.hpp"
#include "base/file.hpp"
#include "base/log.hpp"
#include "input/input-frame.hpp"
#include "document/document-actions.hpp"
#include "document/documents-actions.hpp"
#include "document/workspace.hpp"
#include "ui/panels/canvas/canvas-panel.hpp"
#include "ui/panels/document-tabs-panel.hpp"
#include "ui/panels/graph/graph-panel.hpp"
#include "ui/panels/history-panel.hpp"
#include "ui/panels/info-panel.hpp"
#include "ui/panels/intro-panel.hpp"
#include "ui/keymap.hpp"
#include "ui/panels/navigator-panel.hpp"
#include "ui/panels/color-panel.hpp"
#include "ui/panels/tool-properties-panel.hpp"
#include "ui/panels/toolbox-panel.hpp"
#include "ui/style/ui-style.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // FindHoveredWindowEx
#include <algorithm>
#include <cmath>
#include <cstdio>

UserInterface::UserInterface()
{
    // Every panel, in View menu order. Where each docks and when it draws are its own (PanelInfo).
    m_canvasPanel = addPanel<CanvasPanel>();
    m_graphPanel  = addPanel<GraphPanel>();
    addPanel<ToolboxPanel>();
    addPanel<DocumentTabsPanel>();   // under the toolbar: bars stack in this order
    addPanel<Navigator>();
    addPanel<ColorPanel>();
    addPanel<ToolPropertiesPanel>();
    addPanel<HistoryPanel>();
    addPanel<InfoPanel>();

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

bool UserInterface::init(GpuDevice& gpu, Documents& documents, const InputFrame& input, const HostInfo& host)
{
    m_input = &input;
    m_host  = &host;
    m_documents = &documents;
    // What a user can do to the app and the open documents; the active document's own come
    // through it. Panels add their own interface actions (framing, selecting, opening menus) as they init.
    registerAppActions();
    registerDocumentsActions(m_documentActions, documents);
    registerDocumentRequests();
    bool ok = m_canvas.init(gpu);
    m_intro.init(gpu);
    for (const auto& d : documents.all()) m_canvas.open(d->id(), d->workspace());
    m_documentsEvents = documents.changed.connect([this](const DocumentsEvent& e) { documentsChanged(e); });
    m_context.emplace(UiContext{ gpu, documents, m_actions, m_tools, input, m_owner, m_canvas, host });
    for (PanelEntry& e : m_panels) {
        e.panel->m_ui = &*m_context;
        ok = e.panel->init() && ok;   // one failing leaves the rest working
    }
    // Every action is there now; a document's through a stand-in, so their keys are bound with none open.
    {
        Document keys;
        m_actions.delegateTo(&keys.actions());
        bindDefaultKeys(m_actions);
    }
    m_actions.delegateTo(documents.empty() ? nullptr : &documents.active().actions());
    m_canvas.activate(documents.activeId());
    m_initialized = true;
    return ok;
}

void UserInterface::documentsChanged(const DocumentsEvent& e)
{
    switch (e.type) {
    case DocumentsEvent::Type::Opened:
        m_canvas.open(e.id, m_documents->find(e.id)->workspace());
        for (PanelEntry& p : m_panels) p.panel->documentsChanged(e);
        break;
    case DocumentsEvent::Type::Closing:   // it's still there: everything lets go of it, the engine last
        if (e.id == m_documents->active().id()) m_actions.delegateTo(nullptr);
        for (auto p = m_panels.rbegin(); p != m_panels.rend(); ++p) p->panel->documentsChanged(e);
        m_canvas.close(e.id);
        if (m_saving && m_saving->document == e.id) m_saving.reset();
        if (m_confirm && m_confirm->document == e.id) {   // gone without an answer: on to the next
            m_confirm.reset();
            if (m_quitting) askNextBeforeQuit();
        }
        break;
    case DocumentsEvent::Type::Activated:
        m_actions.delegateTo(&m_documents->find(e.id)->actions());
        m_canvas.activate(e.id);
        for (PanelEntry& p : m_panels) p.panel->documentsChanged(e);
        break;
    }
}

void UserInterface::registerDocumentRequests()
{
    for (const Action& a : m_documentActions.actions()) {
        const ActionId id = a.id;
        m_actions.add({ .id = id, .title = a.title, .kind = ActionKind::Interface, .args = a.args,
                        .enabled = [this, id] { return m_documentActions.find(id)->isEnabled(); },
                        .run = [this, id](const ActionArgs& args) {
            m_documentRequests.push_back({ id, args });
            return true;
        } });
    }
}

void UserInterface::applyDocumentRequests()
{
    std::vector<std::pair<ActionId, ActionArgs>> requests = std::move(m_documentRequests);
    m_documentRequests.clear();
    for (auto& [id, args] : requests) {
        const DocumentId document = args.get(DocArgs::kDocument, m_documents->activeId());
        const std::string file = File::name(args.get(DocArgs::kPath, std::string()));
        const ActionResult r = m_documentActions.run(id, std::move(args));
        if (id == Actions::DocSave && m_saving && m_saving->document == document) answered(bool(r));   // the question's "Yes"
        if (r || r.why.empty()) continue;
        if (id == Actions::DocOpen) m_notices.post("Couldn't open " + file + ": " + r.why);
        if (id == Actions::DocSave) m_notices.post("Couldn't save: " + r.why);
    }
}

void UserInterface::openFile(std::string path)
{
    if (!Document::isFileName(path)) {
        m_importer.open(std::move(path));
        return;
    }
    const std::lock_guard lock(m_inboxLock);
    m_toOpen.push_back(std::move(path));
}

void UserInterface::saveTo(DocumentId document, std::string path)
{
    const std::lock_guard lock(m_inboxLock);
    m_toSave.push_back({ document, std::move(path) });
}

void UserInterface::takeInbox()
{
    std::vector<std::string> open;
    std::vector<std::pair<DocumentId, std::string>> save;
    {
        const std::lock_guard lock(m_inboxLock);
        open.swap(m_toOpen);
        save.swap(m_toSave);
    }
    for (std::string& path : open) m_actions.run(Actions::DocOpen, { { DocArgs::kPath, std::move(path) } });
    for (auto& [document, path] : save) {
        if (path.empty()) {   // the dialog was cancelled: not saved, so nothing closes
            if (m_saving && m_saving->document == document) answered(false);
            continue;
        }
        m_actions.run(Actions::DocSave, { { DocArgs::kDocument, document }, { DocArgs::kPath, std::move(path) } });
    }
}

void UserInterface::requestQuit()
{
    m_actions.run(Actions::AppQuit);
}

void UserInterface::shutdown()
{
    if (!m_initialized) return;
    m_documentsEvents.disconnect();
    for (auto e = m_panels.rbegin(); e != m_panels.rend(); ++e) e->panel->shutdown();
    m_canvas.shutdown();   // after every view of it
    m_actions.delegateTo(nullptr);
    m_initialized = false;
}


bool UserInterface::drawn(const PanelEntry& e) const
{
    return e.info.slot == DockSlot::Toolbar || !m_documents->empty();
}

void UserInterface::releaseGpu()
{
    for (auto e = m_panels.rbegin(); e != m_panels.rend(); ++e) e->panel->releaseGpu();
    m_intro.releaseGpu();
    m_canvas.releaseGpu();   // after every view of it
}

bool UserInterface::restoreGpu(GpuDevice& gpu)
{
    const bool ok = m_canvas.restoreGpu(gpu);   // before every view of it
    m_intro.restoreGpu(gpu);
    for (PanelEntry& e : m_panels) e.panel->restoreGpu();
    return ok;
}

Clock::Seconds UserInterface::nextFrameIn() const
{
    // Imports wake the loop when they finish; a stroke changes nothing while the pen rests.
    // A closed panel needs no frames (each knows it's closed).
    Clock::Seconds next = std::min(m_canvas.nextFrameIn(), m_notices.nextFrameIn());
    if (m_documents)
        for (const auto& d : m_documents->all())
            if (!d->workspace().historySettled()) next = std::min(next, kFrameNow);   // until it is
    for (const PanelEntry& e : m_panels)
        if (drawn(e)) next = std::min(next, e.panel->nextFrameIn());
    return next;
}

void UserInterface::renderGpu(WGPUCommandEncoder enc)
{
    m_canvas.beginFrame();   // first: the output's tiles, for every view of them
    for (const PanelEntry& e : m_panels)
        if (drawn(e)) e.panel->renderGpu(enc);
    m_canvas.renderThumbnails(enc);   // last: the least urgent
}

void UserInterface::draw()
{
    takeInbox();
    applyDocumentRequests();   // last frame's (its pictures are shown now): before anything draws
    if (m_documents->empty() && m_importer.ready()) m_documentActions.run(Actions::DocNew);   // an image lands in a new one
    const ImageImporter::Polled polled = m_importer.poll(m_actions);
    for (const ImageImporter::Imported& i : polled.imported)
        if (std::holds_alternative<ImageImporter::InGraph>(i.placement))
            m_actions.run(Actions::SelectionSet, selectionArgs(NodeSelection::only(i.node)));
    for (const std::string& why : polled.failed) m_notices.post("Couldn't import " + why);
    if (m_canvas.outOfMemory() && !m_toldOutOfMemory) m_notices.post("Out of GPU memory: parts of the picture can't be shown");
    m_toldOutOfMemory = m_canvas.outOfMemory();
    m_owner.beginFrame();
    if (!m_owner.typing()) m_actions.dispatch(*m_input, m_owner.keyboard());   // the frame's shortcuts, all of them, before the panels
    applyDocumentRequests();   // a key's (Ctrl+N): nothing is drawn yet
    drawMenuBar();

    // Bars under the menu bar, then the dockspace takes the rest (with no document open, the
    // intro does). Then everything else, Edit panels before Show panels (DrawPhase).
    for (PanelEntry* e : m_drawOrder)
        if (e->info.slot == DockSlot::Toolbar) e->panel->draw();
    if (m_documents->empty()) m_intro.draw(m_actions);
    else m_dockspace.draw();
    for (PanelEntry* e : m_drawOrder)
        if (e->info.slot != DockSlot::Toolbar && drawn(*e)) e->panel->draw();

    dispatchDrops();   // after the panels: their windows and rects are this frame's
    drawConfirm();
    m_notices.draw();
    // A control held or a key down: a coalescing setting (a slider drag, a held [ ) is still going.
    m_actions.endFrame(ImGui::IsAnyItemActive() || m_input->anyButtonHeld() || !m_input->held.empty());
    settleHistories();
}

void UserInterface::settleHistories()
{
    // The active document first; the others after, on what's left.
    if (m_documents->empty()) return;
    const Deadline until = Deadline::in(kHistoryShare);
    if (!m_documents->active().workspace().settleHistory(until)) return;
    for (const auto& d : m_documents->all())
        if (!d->workspace().settleHistory(until)) return;
}

void UserInterface::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File")) {
        Widgets::actionMenuItem(m_actions, Actions::DocNew);
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
        if (Document::isFileName(d.path))   // a project: opened, wherever it lands
            m_actions.run(Actions::DocOpen, { { DocArgs::kPath, std::move(d.path) } });
        else if (m_canvasPanel->dropTarget(top, d.pos, world))
            m_importer.open(std::move(d.path), ImageImporter::OnCanvas{ world.x, world.y });
        else if (m_graphPanel->dropTarget(top, d.pos, nodePos))
            m_importer.open(std::move(d.path), ImageImporter::InGraph{ nodePos.x, nodePos.y });
        else
            Log::info("drop", "ignored (not on the canvas or the graph): %s", d.path.c_str());
    }
    m_drops.clear();
}

void UserInterface::ask(DocumentId document, bool quitting)
{
    m_confirm = Confirm{ .quitting = quitting, .document = document };
}

void UserInterface::askNextBeforeQuit()
{
    for (const auto& d : m_documents->all())
        if (d->unsaved() && std::find(m_keptUnsaved.begin(), m_keptUnsaved.end(), d->id()) == m_keptUnsaved.end()) {
            ask(d->id(), true);
            return;
        }
    m_quitting = false;
    m_quit = true;
}

void UserInterface::answered(bool saved)
{
    const Confirm was = *m_saving;
    m_saving.reset();
    if (!saved) stopAsking();   // why is told (a notice), or the dialog was cancelled
    else if (was.quitting) askNextBeforeQuit();
    else m_actions.run(Actions::DocClose, { { DocArgs::kDocument, was.document } });
}

void UserInterface::stopAsking()
{
    m_confirm.reset();
    m_saving.reset();
    m_quitting = false;
    m_keptUnsaved.clear();
}

void UserInterface::drawConfirm()
{
    constexpr const char* kPopup = "Unsaved changes##confirm";
    if (!m_confirm) return;
    const Document* d = m_documents->find(m_confirm->document);
    if (!d) {
        stopAsking();
        return;
    }
    if (!ImGui::IsPopupOpen(kPopup)) ImGui::OpenPopup(kPopup);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));   // the window's middle, as its text changes too
    if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    ImGui::GetCurrentContext()->DimBgRatio = 1.0f;   // the dim behind it at once (ImGui fades it in over a sixth of a second)
    char text[256];
    std::snprintf(text, sizeof text, "Save changes to %s before %s?", d->name().c_str(), m_confirm->quitting ? "quitting" : "closing");
    ImGui::TextUnformatted(text);
    ImGui::Spacing();

    // Three alike, centred under the text.
    const char* labels[] = { "Yes", "No", "Cancel" };
    const float pad = Style::metrics().framePadding, gap = ImGui::GetStyle().ItemSpacing.x;
    float w = 0.0f;
    for (const char* l : labels) w = std::max(w, std::round(ImGui::CalcTextSize(l).x + pad * 4.0f));
    const float row = 3.0f * w + 2.0f * gap;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - row) * 0.5f));
    int answer = -1;
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        if (Widgets::button(labels[i], w)) answer = i;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) answer = 2;

    const Confirm confirm = *m_confirm;
    switch (answer) {
    case 0:   // saved first (asked where, if it has no file yet): answered() goes on once it is
        m_confirm.reset();
        m_saving = confirm;
        if (!m_actions.run(Actions::FileSave, { { DocArgs::kDocument, confirm.document } })) answered(false);   // can't now (an edit going on)
        break;
    case 1:
        m_confirm.reset();
        if (!confirm.quitting) {
            m_actions.run(Actions::DocClose, { { DocArgs::kDocument, confirm.document }, { DocArgs::kDiscard, true } });
        } else {
            m_keptUnsaved.push_back(confirm.document);
            askNextBeforeQuit();   // the next one's question in this one's place
        }
        break;
    case 2:
        stopAsking();
        break;
    default: break;
    }
    if (!m_confirm) {
        ImGui::CloseCurrentPopup();
        ImGui::GetCurrentContext()->DimBgRatio = 0.0f;   // ... and gone at once
    }
    ImGui::EndPopup();
}
