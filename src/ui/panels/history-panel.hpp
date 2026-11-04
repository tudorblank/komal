#pragma once
// The History Tree window: every step of the workspace's history as a small graph, one row per
// step in the order they were made, each branch in its own lane. Click a step to go there
// (history.goTo): undo, redo and every branch, in one place. Steps are picked the way rows are
// everywhere: a click picks one (and goes there), Ctrl+click adds or takes one away, Shift+click
// picks the range from the last one clicked (neither goes anywhere). Del deletes the picked ones
// for good (history.delete): their children take their place.
//
//   0  ●  Start             its number first, then the tree; the way back to the start: accent
//      │
//   1  ●  Brush Stroke      where you are: a ringed dot, a bright label
//   2  ├─○  Connect         a branch: its own lane, off the step it branched from
//   3  ○  Move Nodes        ahead, on redo's way: dimmer
//
// The numbers keep their column on the left; the tree grows to the right as branches add lanes.
//
// Laid out again only when a step is added. A step continues its parent's lane if it's the first
// step made from there; every later one starts a branch in the lowest lane free from that row down.

#include "document/history.hpp"
#include "ui/panels/panel-base.hpp"

#include <cstdint>
#include <vector>


class HistoryPanel : public Panel {
public:
    PanelInfo info() const override;
    bool init() override;
    void documentsChanged(const DocumentsEvent& e) override;   // another document's history: laid out anew
    void draw() override;

private:
    void layout(const History& history);
    void pick(StepId step, bool add, bool range);   // click, Ctrl+click, Shift+click
    bool picked(StepId step) const;

    std::vector<int> m_lane;   // per step
    int      m_lanes = 1;
    size_t   m_laidOut = 0;               // steps when last laid out (history only grows)
    StepId   m_shownCurrent = kNoStep;    // the current step when last scrolled to
    std::vector<StepId> m_picked;         // the steps picked (view state: never the document's), sorted
    StepId   m_anchor = kNoStep;          // where a Shift+click range starts
};
