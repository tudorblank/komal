#pragma once
#include "raster/raster-data.h"

#include <functional>
#include <utility>
#include <vector>

// One user action: the tile deltas of every raster it touched.
using RasterSteps = std::vector<std::pair<RasterId, TileDelta>>;

// Undo tree (branches are kept: undoing then making a new edit starts a sibling branch).
// Moving to any node undoes up to the common ancestor and redoes down to the target.
class History {
public:
    // Must swap `delta` into raster `id` (applyStep) and notify whoever shows it.
    using ApplyFn = std::function<void(RasterId id, TileDelta& delta)>;

    History() { m_nodes.emplace_back(); }   // node 0 = the initial state

    uint32_t current() const { return m_current; }
    bool canUndo() const { return m_current != 0; }
    bool canRedo() const { return m_nodes[m_current].redoChild != kNone; }

    // Records an action; no-op actions are dropped, so undo never "does nothing".
    bool push(RasterSteps steps);
    void goTo(uint32_t target, const ApplyFn& apply);
    bool undo(const ApplyFn& apply);
    bool redo(const ApplyFn& apply);   // follows the branch most recently undone or made

    size_t bytes() const;

private:
    static constexpr uint32_t kNone = 0xFFFFFFFFu;
    struct Node {
        uint32_t parent = kNone;
        uint32_t depth = 0;
        uint32_t redoChild = kNone;
        std::vector<uint32_t> children;
        RasterSteps steps;
        size_t bytes = 0;
    };

    void flip(uint32_t id, const ApplyFn& apply);   // apply and unapply are the same swap

    std::vector<Node> m_nodes;
    uint32_t m_current = 0;
};
