#pragma once
// The workspace's changes as history steps: the History tree, the step open now (what it opened
// with), and what changed since the current step was captured. A step captures the graph when it
// closes, only if a graph event came, and then anew only for the nodes those events named; and
// the pixels anew only for the rasters whose revision moved. The rest share the step before's.
//
//   steps.begin("Connect");    // nested ones merge into the outermost
//   steps.touched(node);       // every graph event, by the node it names (Workspace::emit)
//   if (steps.end()) ...       // a step was kept: tell HistoryChanged

#include "document/graph-state.hpp"
#include "document/history.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <memory>
#include <string>
#include <vector>

class StepRecorder {
public:
    // The workspace's: they outlive it.
    StepRecorder(const NodeRegistry& nodes, const Artboards& artboards) : m_nodes(nodes), m_artboards(artboards) {}

    void start();   // the graph now: where undo goes back to
    void touched(NodeId node) { m_touched.insert(node); }
    // `pixelsSoFar`: pixels changed since the current step are this step's (an edit made before
    // its step opened: a stroke, an import); else they're the current step's, caught up first.
    void begin(std::string label, bool pixelsSoFar = false);
    bool end();     // the outermost: true when it changed something and was kept
    bool open() const { return m_depth > 0; }
    // History moves to `step`: what it holds, for the workspace to restore.
    const HistoryStep& goTo(StepId step);
    void remove(const std::vector<StepId>& steps) { m_history.remove(steps); }   // History::remove
    void synced();   // the live graph and pixels are the current step's (restored)
    const History& history() const { return m_history; }

private:
    std::shared_ptr<const GraphState> capture(const std::shared_ptr<const GraphState>& previous) const;
    // Every raster's state: `previous`'s for those whose pixels are what it holds, a new snapshot
    // for the rest (made, or edited, since).
    RasterStates captureRasters(const RasterStates& previous) const;
    bool rastersMoved() const;   // a raster's pixels changed since the current step was captured

    const NodeRegistry& m_nodes;
    const Artboards&    m_artboards;
    History m_history;
    int     m_depth = 0;   // open steps (nested ones merge)
    std::string m_label;
    std::shared_ptr<const GraphState> m_before; // the graph when it opened
    RasterStates m_beforePixels;                // ... and the pixels
    absl::flat_hash_map<RasterId, uint64_t> m_synced;   // each raster's revision when it was what the current step holds
    absl::flat_hash_set<NodeId> m_touched;      // nodes graph events named since the graph last matched the current step's
};
