#pragma once
// The workspace's changes as history steps: the History tree, the step open now (what it opened
// with, the raster commits made in it), and which nodes changed since the graph was last captured.
// A step captures the graph when it closes, only if a graph event came, and then anew only for
// the nodes those events named: the rest share the step before's states.
//
//   steps.begin("Connect");    // nested ones merge into the outermost
//   steps.touched(node);       // every graph event, by the node it names (Workspace::emit)
//   if (steps.end()) ...       // a step was kept: tell HistoryChanged

#include "document/graph-state.hpp"
#include "document/history.hpp"

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
    void begin(std::string label);
    bool end();     // the outermost: true when it changed something and was kept
    bool open() const { return m_depth > 0; }
    void commit(const RasterCommitRef& c) { m_commits.push_back(c); }   // into the open step

    // History moves to `step` (its raster commits flipped on the way): the graph to restore there.
    std::shared_ptr<const GraphState> goTo(StepId step, const History::FlipRaster& flip);
    void synced() { m_touched.clear(); }   // the live graph is the current step's (restored)
    const History& history() const { return m_history; }

private:
    std::shared_ptr<const GraphState> capture(const std::shared_ptr<const GraphState>& previous) const;

    const NodeRegistry& m_nodes;
    const Artboards&    m_artboards;
    History m_history;
    int     m_depth = 0;   // open steps (nested ones merge)
    std::string m_label;
    std::vector<RasterCommitRef> m_commits;     // made while it's open
    std::shared_ptr<const GraphState> m_before; // the graph when it opened
    absl::flat_hash_set<NodeId> m_touched;      // nodes graph events named since the graph last matched the current step's
};
