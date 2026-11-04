// The app's own actions: quitting, closing a document (asking first), importing, the panels and the layout. App-state actions are
// logged and never undone. The rest register their own: document edits and gestures in
// document/document-actions, tools theirs (ToolManager::registerActions), panels their interface
// actions as they init (framing, selecting, opening menus; not logged).
#include "ui-main.hpp"
#include "document/documents-actions.hpp"

void UserInterface::registerAppActions()
{
    using K = ActionKind;

    // ---------------------------------------------------------------- app
    // Each unsaved document asked about first, in turn (drawConfirm).
    m_actions.add({ .id = Actions::AppQuit, .title = "Quit", .kind = K::AppState, .run = [this] {
        if (m_quitting || m_saving) return;   // asking already
        m_quitting = true;
        m_keptUnsaved.clear();
        askNextBeforeQuit();
    } });
    // Open / Save / Save As: the OS's dialogs pick the file, doc.open and doc.save use it.
    const auto settled = [this] { return m_documentActions.find(Actions::DocSave)->isEnabled(); };
    m_actions.add({ .id = Actions::FileOpen, .title = "Open...", .kind = K::Interface,
                    .run = [this] { if (m_host->showOpenDialog) m_host->showOpenDialog(); } });
    m_actions.add({ .id = Actions::FileSaveAs, .title = "Save As...", .kind = K::Interface, .args = { Arg::id(DocArgs::kDocument).optional() },
                    .enabled = settled, .run = [this](const ActionArgs& a) {
        const Document* d = m_documents ? m_documents->find(a.get(DocArgs::kDocument, m_documents->activeId())) : nullptr;
        if (!d || !m_host->showSaveDialog) return false;
        m_host->showSaveDialog(d->id(), d->path().empty() ? d->name() + Document::kExtension : d->path());
        return true;
    } });
    // Its own file if it has one, else asked where (Save As).
    m_actions.add({ .id = Actions::FileSave, .title = "Save", .kind = K::Interface, .args = { Arg::id(DocArgs::kDocument).optional() },
                    .enabled = settled, .run = [this](const ActionArgs& a) {
        const Document* d = m_documents ? m_documents->find(a.get(DocArgs::kDocument, m_documents->activeId())) : nullptr;
        if (!d) return false;
        if (d->path().empty()) return bool(m_actions.run(Actions::FileSaveAs, { { DocArgs::kDocument, d->id() } }));
        return bool(m_actions.run(Actions::DocSave, { { DocArgs::kDocument, d->id() } }));
    } });
    // Closes a document (the active one if none is given), asking first if it's unsaved; doc.close does it.
    m_actions.add({ .id = Actions::FileClose, .title = "Close", .kind = K::Interface, .args = { Arg::id(DocArgs::kDocument).optional() },
                    .run = [this](const ActionArgs& a) {
        const Document* d = m_documents ? m_documents->find(a.get(DocArgs::kDocument, m_documents->activeId())) : nullptr;
        if (!d) return false;
        if (d->unsaved()) {
            if (!m_confirm && !m_saving) ask(d->id(), false);
            return true;
        }
        return bool(m_actions.run(Actions::DocClose, { { DocArgs::kDocument, d->id() } }));
    } });
    // Opens the dialog; the import itself is logged when it lands (image.import). Primary: Cmd on macOS, Ctrl elsewhere.
    m_actions.add({ .id = Actions::FileImport, .title = "Import Image...", .kind = K::Interface,
                    .run = [this] { if (m_host->showImportDialog) m_host->showImportDialog(); } });

    // ---------------------------------------------------------------- panels
    // "view.<id>" per panel: the menu and the window's close button both run it.
    for (const PanelEntry& e : m_panels) {
        Panel* panel = e.panel.get();
        m_actions.add({ .id = panel->toggleAction(), .title = e.info.title, .kind = K::AppState,
                        .checked = [panel] { return panel->isOpen(); }, .run = [panel] { panel->m_open = !panel->m_open; } });
    }
    // The docked layout as it starts, each panel in it open as it starts (floating ones stay as they are).
    m_actions.add({ .id = Actions::ViewResetLayout, .title = "Reset layout", .kind = K::AppState, .run = [this] {
        m_dockspace.reset();
        for (const PanelEntry& e : m_panels)
            if (e.info.slot != DockSlot::Floating) e.panel->m_open = e.info.openByDefault;
    } });

    // ---------------------------------------------------------------- tools
    m_tools.registerActions(m_actions);   // picking them, their settings, the colours
}
