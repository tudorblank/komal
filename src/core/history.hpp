#pragma once
#include "raster/raster-data.hpp"

#include <functional>
#include <vector>

// per-raster history step (ref)
struct RasterCommitRef {
    RasterId       raster = kNoRasterId;
    RasterCommitId commit = kNoRasterCommit;
};

// global step
struct HistoryStep {
    std::vector<RasterCommitRef> rasterCommits;
    bool empty() const { return rasterCommits.empty(); }
};

class History {
public:
    // Undoes the commit if it is applied, redoes it if not: a commit flips both ways.
    using FlipRaster = std::function<void(const RasterCommitRef& commit)>;

    History() { m_nodes.emplace_back(); }   // node 0 = the initial state

    uint32_t current() const { return m_current; }
    bool canUndo() const { return m_current != 0; }
    bool canRedo() const { return m_nodes[m_current].redoChild != kNone; }

    // Records an action; empty ones are dropped, so undo never "does nothing".
    bool push(HistoryStep step);
    void goTo(uint32_t target, const FlipRaster& flip);
    bool undo(const FlipRaster& flip);
    bool redo(const FlipRaster& flip);   // follows the branch most recently undone or made

private:
    static constexpr uint32_t kNone = 0xFFFFFFFFu;
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
