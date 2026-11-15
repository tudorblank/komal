#pragma once
// A panel's own window, open for the scope (a panel's, so here: widgets know no panels).

#include "command/actions.hpp"
#include "ui/panels/panel-base.hpp"

#include <imgui.h>

namespace Widgets {

// A panel's window, open for the scope: its title, and its close button running its View toggle.
// Padded: panelPadding on every side; a view (the canvas, the graph) fills it edge to edge.
//
//   Widgets::PanelWindow window(*this, actions);
//   if (!window) return;
class PanelWindow {
public:
    enum class Padding { Panel, None };
    PanelWindow(const Panel& panel, ActionRegistry& actions, ImGuiWindowFlags flags = 0, Padding padding = Padding::Panel);
    ~PanelWindow();
    PanelWindow(const PanelWindow&) = delete;
    PanelWindow& operator=(const PanelWindow&) = delete;
    explicit operator bool() const { return m_shown; }   // false: collapsed or tabbed away

private:
    bool m_shown = false;
};
// A bar under the menu bar (the Toolbox, the projects' strip), open for the scope: `height` tall,
// `padding` inside, on `back`. The dockspace lays out below it. Its items space themselves
// (SameLine with their style's gap).
class BarWindow {
public:
    BarWindow(const Panel& panel, float height, ImVec2 padding, ImU32 back);
    ~BarWindow();
    BarWindow(const BarWindow&) = delete;
    BarWindow& operator=(const BarWindow&) = delete;
    explicit operator bool() const { return m_shown; }

private:
    bool m_shown = false;
};

}
