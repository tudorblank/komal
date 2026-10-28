// Every app-state action: the one list of what changes the app but not the document (panels,
// layout, tools, brush settings, colours, quitting). Logged, never undone. Document edits and
// gestures are in core/document-actions; the panels add their own interface actions (framing,
// selecting, opening menus), which aren't logged.
#include "ui-main.hpp"
#include "ui/panel-titles.hpp"
#include "ui/tools/brush-tool.hpp"

#include <algorithm>
#include <cmath>

void UserInterface::registerAppActions()
{
    using K = ActionKind;

    // ---------------------------------------------------------------- app
    m_actions.add({ .id = "app.quit", .title = "Quit", .kind = K::AppState, .run = [this] { m_quit = true; } });
    // Opens the dialog; the import itself is logged when it lands (image.import). Primary: Cmd on macOS, Ctrl elsewhere.
    m_actions.add({ .id = "file.import", .title = "Import Image...", .kind = K::Interface, .shortcuts = { { SDLK_O, ModPrimary } },
                    .run = [this] { m_importer.showOpenDialog(m_window); } });

    // ---------------------------------------------------------------- panels
    // Each panel's own open flag: the menu and the window's close button both run its toggle.
    const auto panel = [this](const char* id, const char* title, bool& shown) {
        m_actions.add({ .id = id, .title = title, .kind = K::AppState, .checked = [&shown] { return shown; },
                        .run = [&shown] { shown = !shown; } });
    };
    panel("view.canvas",          PanelTitle::kCanvas,         m_canvasPanel.open());
    panel("view.graph",           PanelTitle::kGraph,          m_graphPanel.open());
    panel("view.tools",           "Toolbar",                   m_toolbox.open());
    panel("view.navigator",       PanelTitle::kNavigator,      m_navigator.open());
    panel("view.color",           PanelTitle::kColor,          m_colorPanel.open());
    panel("view.toolProperties",  PanelTitle::kToolProperties, m_toolProperties.open());
    panel("view.historyTree",     PanelTitle::kHistoryTree,    m_historyPanel.open());
    panel("view.info",            PanelTitle::kInfo,           m_info.open());
    panel("view.imguiDemo",       "ImGui demo",                m_showDemo);
    m_actions.add({ .id = "view.resetLayout", .title = "Reset layout", .kind = K::AppState, .run = [this] {
        m_dockspace.reset();
        for (bool* shown : { &m_canvasPanel.open(), &m_graphPanel.open(), &m_toolbox.open(), &m_toolProperties.open(),
                             &m_colorPanel.open(), &m_navigator.open(), &m_historyPanel.open() })
            *shown = true;
        m_info.open() = false;   // on demand (View > Info)
    } });

    // ---------------------------------------------------------------- tools
    // "tool.<name>" per tool (its own key, no modifiers), never under a stroke or a drag.
    const auto idle = [this] { return !m_tools.active().busy(); };
    BrushTool& brush = m_tools.brush();
    for (const auto& t : m_tools.tools()) {
        Tool* tool = t.get();
        m_actions.add({ .id = toolActionId(*tool), .title = tool->name(), .kind = K::AppState, .shortcuts = { { tool->shortcut() } },
                        .enabled = idle, .checked = [this, tool] { return &m_tools.selected() == tool; },
                        .run = [this, tool, &brush] {
                            m_tools.select(*tool, m_actions);
                            if (tool == &brush) brush.setErase(false);
                        } });
    }
    m_actions.add({ .id = "tool.eraser", .title = "Eraser", .kind = K::AppState, .shortcuts = { { SDLK_E } }, .enabled = idle,
                    .run = [this, &brush] {
                        m_tools.select(brush, m_actions);
                        brush.setErase(true);
                    } });

    // ---------------------------------------------------------------- brush settings
    // Set from Tool Properties: a slider drag or a held key logs once, with where it ended.
    const auto setting = [this](const char* id, const char* title, auto apply) {
        m_actions.add({ .id = id, .title = title, .kind = K::AppState, .coalesce = true, .run = std::move(apply) });
    };
    BrushParams& bp = brush.params();
    setting("brush.size",     "Brush Size",     [&bp](const ActionArgs& a) { bp.diameter = std::clamp(float(a.number("value")), 1.0f, BrushTool::kMaxDiameter); });
    setting("brush.hardness", "Brush Hardness", [&bp](const ActionArgs& a) { bp.hardness = std::clamp(float(a.number("value")), 0.0f, 1.0f); });
    setting("brush.opacity",  "Brush Opacity",  [&bp](const ActionArgs& a) { bp.opacity = uint8_t(std::clamp(a.number("value"), 0.0, 1.0) * 255.0 + 0.5); });
    setting("brush.flow",     "Brush Flow",     [&bp](const ActionArgs& a) { bp.flow = std::clamp(float(a.number("value")), 0.01f, 1.0f); });
    setting("brush.spacing",  "Brush Spacing",  [&bp](const ActionArgs& a) { bp.spacing = std::clamp(float(a.number("value")), 0.02f, 1.0f); });
    setting("brush.antialias",    "Antialiased Edges", [&bp](const ActionArgs& a) { bp.antialias = a.flag("on"); });
    setting("brush.pressureSize", "Pressure Size",     [&bp](const ActionArgs& a) { bp.pressureSize = a.flag("on"); });
    setting("brush.pressureFlow", "Pressure Flow",     [&bp](const ActionArgs& a) { bp.pressureFlow = a.flag("on"); });
    setting("brush.mode",     "Brush Mode",     [&brush](const ActionArgs& a) { brush.setErase(a.flag("erase")); });

    // [ and ] while the brush is the canvas's tool: keys for brush.size (which logs where it ended).
    const auto brushActive = [this, &brush] { return &m_tools.active() == &brush; };
    m_actions.add({ .id = "brush.bigger", .title = "Bigger Brush", .kind = K::Interface, .shortcuts = { { SDLK_RIGHTBRACKET } },
                    .scope = ActionScope::Canvas, .repeats = true, .enabled = brushActive, .run = [this, &bp] {
                        m_actions.run("brush.size", { { "value", std::min(BrushTool::kMaxDiameter, std::round(bp.diameter * 1.25f + 0.5f)) } });
                    } });
    m_actions.add({ .id = "brush.smaller", .title = "Smaller Brush", .kind = K::Interface, .shortcuts = { { SDLK_LEFTBRACKET } },
                    .scope = ActionScope::Canvas, .repeats = true, .enabled = brushActive, .run = [this, &bp] {
                        m_actions.run("brush.size", { { "value", std::max(1.0f, std::round(bp.diameter / 1.25f)) } });
                    } });

    // ---------------------------------------------------------------- colours
    // target (primary or secondary) r g b a, straight, 0..1.
    ToolColors& colors = m_tools.colors();
    m_actions.add({ .id = "color.set", .title = "Set Color", .kind = K::AppState, .coalesce = true, .run = [&colors](const ActionArgs& a) {
        float* c = a.text("target") == "secondary" ? colors.background : colors.foreground;
        c[0] = float(a.number("r", c[0]));
        c[1] = float(a.number("g", c[1]));
        c[2] = float(a.number("b", c[2]));
        c[3] = float(a.number("a", c[3]));
    } });
    m_actions.add({ .id = "color.swap", .title = "Swap Colors", .kind = K::AppState, .shortcuts = { { SDLK_X } },
                    .run = [&colors] { colors.swap(); } });
}
