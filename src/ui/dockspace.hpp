#pragma once
// The main window's dock layout: the default arrangement (each panel's DockSlot), and how it
// scales with the window. ImGui alone gives the whole size difference to one side; here the
// canvas and graph areas grow, the side column keeps its width, and stacked panels keep their shares.

#include "ui/panel.hpp"

#include <absl/container/flat_hash_map.h>
#include <vector>

class Dockspace {
public:
    void addPanel(const PanelInfo& panel);   // once per panel: where it starts, whether its area grows
    void draw();                         // before the windows that dock into it
    void reset() { m_reset = true; }     // back to the default layout, next draw

private:
    void buildDefault(unsigned int id, float w, float h);

    std::vector<PanelInfo>    m_panels;
    std::vector<unsigned int> m_growing;   // the window ids of panels whose areas grow with the window (Center, Bottom)
    absl::flat_hash_map<unsigned int, float> m_shares;   // dock split -> its first child's share
    bool m_reset = false;
};
