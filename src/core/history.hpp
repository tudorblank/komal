#pragma once
// The project's history: a tree of states. Every step is a node; undo goes to its parent, redo to
// the child last undone or made, and goTo() jumps to any node, across branches. A new step after
// undoing starts a new branch: the undone steps stay, and stay reachable.
//
// A step holds what it changed: the raster commits it made (tiles flip both ways), and the graph
// as it was after it (GraphState, see Project), which undo and redo restore.

#include "raster/raster-data.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct GraphState;

// per-raster history step (ref)
struct RasterCommitRef {
    RasterId       raster = kNoRasterId;
    RasterCommitId commit = kNoRasterCommit;
};

// global step
struct HistoryStep {
    std::string label;                           // what the user did ("Connect", "Brush Stroke")
    std::vector<RasterCommitRef> rasterCommits;
    std::shared_ptr<const GraphState> graph;     // the graph after it
};

class History {
public:
    static constexpr uint32_t kNone = 0xFFFFFFFFu;

    // Undoes the commit if it is applied, redoes it if not: a commit flips both ways.
    using FlipRaster = std::function<void(const RasterCommitRef& commit)>;

    History() { m_nodes.emplace_back().step.label = "Start"; }   // node 0 = the initial state

    uint32_t current() const { return m_current; }
    bool canUndo() const { return m_current != 0; }
    bool canRedo() const { return m_nodes[m_current].redoChild != kNone; }

    // Adds a step after the current one (a new branch if it has children already) and goes there.
    void push(HistoryStep step);
    void goTo(uint32_t target, const FlipRaster& flip);
    bool undo(const FlipRaster& flip);
    bool redo(const FlipRaster& flip);   // follows the branch most recently undone or made

    // ---- the tree, by node id (0 = start; ids grow in the order steps were made)
    size_t size() const { return m_nodes.size(); }
    const HistoryStep& step(uint32_t id) const { return m_nodes[id].step; }
    uint32_t parent(uint32_t id) const { return m_nodes[id].parent; }
    uint32_t redoChild(uint32_t id) const { return m_nodes[id].redoChild; }
    const std::vector<uint32_t>& children(uint32_t id) const { return m_nodes[id].children; }
    void setGraph(uint32_t id, std::shared_ptr<const GraphState> graph) { m_nodes[id].step.graph = std::move(graph); }

private:
    struct Node {
        uint32_t parent = kNone;
        uint32_t depth = 0;
        uint32_t redoChild = kNone;
        std::vector<uint32_t> children;
        HistoryStep step;
    };

    void apply(uint32_t id, bool forward, const FlipRaster& flip);   // forward: redo order; else undo order

    std::vector<Node> m_nodes;
    uint32_t m_current = 0;
};
