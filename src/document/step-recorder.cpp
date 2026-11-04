#include "document/step-recorder.hpp"

#include <utility>

std::shared_ptr<const GraphState> StepRecorder::capture(const std::shared_ptr<const GraphState>& previous) const
{
    return std::make_shared<const GraphState>(previous ? GraphState::capture(m_nodes, m_artboards, *previous, m_touched)
                                                       : GraphState::capture(m_nodes, m_artboards));
}

void StepRecorder::start()
{
    m_history.setGraph(m_history.current(), capture(nullptr));
    synced();
}

void StepRecorder::begin(std::string label)
{
    if (m_depth++ > 0) return;   // inside another: merges into it
    m_label = std::move(label);
    m_commits.clear();
    // Changed outside any step since (nothing should): undo comes back to what it really was.
    const StepId here = m_history.current();
    const std::shared_ptr<const GraphState>& was = m_history.step(here).graph;
    if (!m_touched.empty() || !was) {
        std::shared_ptr<const GraphState> now = capture(was);
        if (!was || !(*was == *now)) m_history.setGraph(here, std::move(now));
        synced();
    }
    m_before = m_history.step(here).graph;
}

bool StepRecorder::end()
{
    if (m_depth == 0 || --m_depth > 0) return false;
    // Captured again only if a graph event came: otherwise it's the state it opened with, shared.
    std::shared_ptr<const GraphState> after = std::move(m_before);
    bool graphChanged = false;
    if (!m_touched.empty()) {
        std::shared_ptr<const GraphState> now = capture(after);
        graphChanged = !(*now == *after);
        if (graphChanged) after = std::move(now);
    }
    synced();   // `after` is the live graph, and the current step's from here on
    if (m_commits.empty() && !graphChanged) return false;   // changed nothing: undo never "does nothing"
    m_history.push({ std::move(m_label), std::exchange(m_commits, {}), std::move(after) });
    return true;
}

std::shared_ptr<const GraphState> StepRecorder::goTo(StepId step, const History::FlipRaster& flip)
{
    m_history.goTo(step, flip);
    return m_history.step(step).graph;
}
