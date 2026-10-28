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

#include <cstdint>
#include <vector>

class ActionRegistry;
class History;

class HistoryPanel {
public:
    void draw(const History& history, ActionRegistry& actions);
    bool& open() { return m_open; }   // shown (View > History Tree, the window's close button)

private:
    void layout(const History& history);

    std::vector<int> m_lane;   // per step
    int      m_lanes = 1;
    size_t   m_laidOut = 0;               // steps when last laid out (history only grows)
    uint32_t m_shownCurrent = 0xFFFFFFFFu;   // the current step when last scrolled to
    bool     m_open = true;
};
