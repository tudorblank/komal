#pragma once
// The workspace's history: a tree of states. Every step is a node; undo goes to its parent, redo to
// the child last undone or made, and goTo() jumps to any node, across branches. A new step after
// undoing starts a new branch: the undone steps stay, and stay reachable.
//
// A step holds the whole workspace as it was after it: the graph (GraphState) and every raster's
// pixels (a RasterSnapshot each). Steps share what they didn't change (a node's state, a raster's
// snapshot, its tiles), so a step costs what it changed; and since each one holds everything,
// none depends on another: going to a step is restoring it, whatever was before.

#include "raster/raster-data.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct GraphState;

// A step: its place in the order steps were made (0 = the start). Its own type.
enum class StepId : uint32_t {};
constexpr StepId kStartStep{ 0 };
constexpr StepId kNoStep{ 0xFFFFFFFFu };

// A raster's pixels as a step leaves them: shared with the steps around it while they're the same.
struct RasterState {
    RasterId raster = kNoRasterId;
    std::shared_ptr<const RasterSnapshot> pixels;
    bool operator==(const RasterState&) const = default;   // the same snapshot
};
using RasterStates = std::vector<RasterState>;   // every raster there was then, by id

struct HistoryStep {
    std::string label;                           // what the user did ("Connect", "Brush Stroke")
    std::shared_ptr<const GraphState> graph;     // the graph after it
    RasterStates rasters;                        // ... and the pixels
};

class History {
public:
    History() { m_nodes.emplace_back().step.label = "Start"; }   // kStartStep: the initial state

    StepId current() const { return m_current; }
    bool canUndo() const { return m_current != kStartStep; }
    bool canRedo() const { return redoChild(m_current) != kNoStep; }

    // Adds a step after the current one (a new branch if it has children already) and goes there.
    void push(HistoryStep step);
    // The current step from now on (what it holds is the workspace's to restore). Redo comes back
    // the way it left: each step it went up from is its parent's redo child.
    void goTo(StepId target);
    // Steps taken out of the tree for good (never Start, never the current one: move off it
    // first). Each one's children take its place among its parent's; every step left is numbered
    // again in the order they were made. Nothing else changes: each step holds all it was.
    void remove(const std::vector<StepId>& steps);

    // ---- the tree (ids grow in the order steps were made: StepId(0) .. StepId(size() - 1))
    size_t size() const { return m_nodes.size(); }
    bool contains(StepId id) const { return size_t(id) < m_nodes.size(); }
    const HistoryStep& step(StepId id) const { return at(id).step; }
    StepId parent(StepId id) const { return at(id).parent; }        // kNoStep for the start
    StepId redoChild(StepId id) const { return at(id).redoChild; }  // the branch most recently undone or made
    const std::vector<StepId>& children(StepId id) const { return at(id).children; }
    void setGraph(StepId id, std::shared_ptr<const GraphState> graph) { at(id).step.graph = std::move(graph); }
    void setRasters(StepId id, RasterStates rasters) { at(id).step.rasters = std::move(rasters); }

private:
    struct Node {
        StepId   parent = kNoStep;
        uint32_t depth = 0;
        StepId   redoChild = kNoStep;
        std::vector<StepId> children;
        HistoryStep step;
    };

    Node&       at(StepId id)       { return m_nodes[size_t(id)]; }
    const Node& at(StepId id) const { return m_nodes[size_t(id)]; }

    std::vector<Node> m_nodes;
    StepId m_current = kStartStep;
};
