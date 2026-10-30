// The app's own actions: quitting, importing, the panels and the layout. App-state actions are
// logged and never undone. The rest register their own: document edits and gestures in
// document/document-actions, tools theirs (ToolManager::registerActions), panels their interface
// actions as they init (framing, selecting, opening menus; not logged).
#include "ui-main.hpp"

void UserInterface::registerAppActions()
{
    using K = ActionKind;

    // ---------------------------------------------------------------- app
    m_actions.add({ .id = Actions::AppQuit, .title = "Quit", .kind = K::AppState, .run = [this] { m_quit = true; } });
    // Opens the dialog; the import itself is logged when it lands (image.import). Primary: Cmd on macOS, Ctrl elsewhere.
    m_actions.add({ .id = Actions::FileImport, .title = "Import Image...", .kind = K::Interface, .shortcuts = { { SDLK_O, ModPrimary } },
                    .run = [this] { m_importer.showOpenDialog(m_host->window); } });

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
