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

// A step: its place in the order steps were made (0 = the start). Its own type.
enum class StepId : uint32_t {};
constexpr StepId kStartStep{ 0 };
constexpr StepId kNoStep{ 0xFFFFFFFFu };

struct RasterCommitRef {
    RasterId       raster = kNoRasterId;
    RasterCommitId commit = kNoRasterCommit;
};

struct HistoryStep {
    std::string label;                           // what the user did ("Connect", "Brush Stroke")
    std::vector<RasterCommitRef> rasterCommits;
    std::shared_ptr<const GraphState> graph;     // the graph after it
};

class History {
public:
    // Undoes the commit if it is applied, redoes it if not: a commit flips both ways.
    using FlipRaster = std::function<void(const RasterCommitRef& commit)>;

    History() { m_nodes.emplace_back().step.label = "Start"; }   // kStartStep: the initial state

    StepId current() const { return m_current; }
    bool canUndo() const { return m_current != kStartStep; }
    bool canRedo() const { return redoChild(m_current) != kNoStep; }

    // Adds a step after the current one (a new branch if it has children already) and goes there.
    void push(HistoryStep step);
    void goTo(StepId target, const FlipRaster& flip);

    // ---- the tree (ids grow in the order steps were made: StepId(0) .. StepId(size() - 1))
    size_t size() const { return m_nodes.size(); }
    bool contains(StepId id) const { return size_t(id) < m_nodes.size(); }
    const HistoryStep& step(StepId id) const { return at(id).step; }
    StepId parent(StepId id) const { return at(id).parent; }        // kNoStep for the start
    StepId redoChild(StepId id) const { return at(id).redoChild; }  // the branch most recently undone or made
    const std::vector<StepId>& children(StepId id) const { return at(id).children; }
    void setGraph(StepId id, std::shared_ptr<const GraphState> graph) { at(id).step.graph = std::move(graph); }

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
    void apply(StepId id, bool forward, const FlipRaster& flip);   // forward: redo order; else undo order

    std::vector<Node> m_nodes;
    StepId m_current = kStartStep;
};
