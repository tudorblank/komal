#include "document/step-recorder.hpp"

#include <algorithm>
#include <utility>

std::shared_ptr<const GraphState> StepRecorder::capture(const std::shared_ptr<const GraphState>& previous) const
{
    return std::make_shared<const GraphState>(previous ? GraphState::capture(m_nodes, m_artboards, *previous, m_touched)
                                                       : GraphState::capture(m_nodes, m_artboards));
}

RasterStates StepRecorder::captureRasters(const RasterStates& previous) const
{
    RasterStates out;
    m_nodes.forEachRaster([&](const RasterRootNode& n) {
        const RasterData& r = n.raster();
        const auto was = std::find_if(previous.begin(), previous.end(), [&](const RasterState& s) { return s.raster == r.id(); });
        const auto synced = m_synced.find(r.id());
        if (was != previous.end() && synced != m_synced.end() && synced->second == r.revision()) out.push_back(*was);
        else out.push_back({ r.id(), r.snapshot() });
    });
    return out;
}

bool StepRecorder::rastersMoved() const
{
    bool moved = false;
    m_nodes.forEachRaster([&](const RasterRootNode& n) {
        const auto synced = m_synced.find(n.raster().id());
        moved |= synced == m_synced.end() || synced->second != n.raster().revision();
    });
    return moved;
}

void StepRecorder::synced()
{
    m_touched.clear();
    m_synced.clear();
    m_nodes.forEachRaster([&](const RasterRootNode& n) { m_synced.emplace(n.raster().id(), n.raster().revision()); });
}

void StepRecorder::start()
{
    m_history.setGraph(m_history.current(), capture(nullptr));
    m_history.setRasters(m_history.current(), captureRasters({}));
    synced();
}

void StepRecorder::begin(std::string label, bool pixelsSoFar)
{
    if (m_depth++ > 0) return;   // inside another: merges into it
    m_label = std::move(label);
    // Changed outside any step since (nothing should): undo comes back to what it really was.
    const StepId here = m_history.current();
    const std::shared_ptr<const GraphState>& was = m_history.step(here).graph;
    if (!m_touched.empty() || !was || (!pixelsSoFar && rastersMoved())) {
        std::shared_ptr<const GraphState> now = capture(was);
        if (!was || !(*was == *now)) m_history.setGraph(here, std::move(now));
        m_history.setRasters(here, captureRasters(m_history.step(here).rasters));
        synced();
    }
    m_before = m_history.step(here).graph;
    m_beforePixels = m_history.step(here).rasters;
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
    RasterStates pixels = captureRasters(m_beforePixels);
    const bool pixelsChanged = pixels != m_beforePixels;
    m_beforePixels.clear();
    synced();   // `after` and `pixels` are the live workspace, and the current step's from here on
    if (!graphChanged && !pixelsChanged) return false;   // changed nothing: undo never "does nothing"
    m_history.push({ std::move(m_label), std::move(after), std::move(pixels) });
    return true;
}

const HistoryStep& StepRecorder::goTo(StepId step)
{
    m_history.goTo(step);
    return m_history.step(step);
}
