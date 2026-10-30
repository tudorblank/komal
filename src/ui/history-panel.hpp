#pragma once
// The History Tree window: every step of the project's history as a small graph, one row per
// step in the order they were made, each branch in its own lane. Click a step to go there
// (history.goTo): undo, redo and every branch, in one place.
//
//   ●  Start             the way back to the start: accent
//   │
//   ●  Brush Stroke      where you are: a ringed dot, a bright label
//   ├─○  Connect         a branch: its own lane, off the step it branched from
//   ○  Move Nodes        ahead, on redo's way: dimmer
//
// Laid out again only when a step is added. A step continues its parent's lane if it's the first
// step made from there; every later one starts a branch in the lowest lane free from that row down.

#include "document/history.hpp"
#include "ui/panel.hpp"

#include <cstdint>
#include <vector>


class HistoryPanel : public Panel {
public:
    PanelInfo info() const override;
    bool init() override;
    void draw() override;

private:
    void layout(const History& history);

    std::vector<int> m_lane;   // per step
    int      m_lanes = 1;
    size_t   m_laidOut = 0;               // steps when last laid out (history only grows)
    StepId   m_shownCurrent = kNoStep;    // the current step when last scrolled to
};
