#pragma once
// What changed, told to every listener right after the change is made (the workspace's state is
// final by then). Views keep their own items and update them from these; nothing re-reads the
// whole workspace. Pixels travel separately, as dirty tiles through the node graph.

#include "node/node.hpp"

#include <functional>

enum class WorkspaceEventType : uint8_t {
    NodeAdded,             // node
    NodeRemoved,           // node (its links are removed first)
    NodeMoved,             // node: its editor position
    NodeChanged,           // node: a value (signal scale, move offset, label, raster locks)
    LinkAdded,             // source feeds node at slot: a single input (Input, Mask)
    LinkRemoved,           // source no longer feeds node at slot
    LayersChanged,         // node: a stack's layers were added, removed, replaced or reordered (or regrouped)
    ArtboardsChanged,      // the master's artboards: one added, deleted or changed
    PreviewChanged,        // an interactive edit started, moved, was applied or cancelled
    ActiveRasterChanged,
    RasterEdited,          // pixels of `node` changed (edit, undo, redo)
    HistoryChanged,
    SelectionChanged,      // which nodes are selected (Workspace::selection())
    CanvasViewChanged,     // where the canvas looks, or its size (Workspace::canvasCamera(), canvasViewport())
};

struct WorkspaceEvent {
    WorkspaceEventType type;
    NodeId    node   = kNoNodeId;
    NodeId    source = kNoNodeId;   // Link*
    InputSlot slot;                 // Link*
};

// How a block that changes the graph tells (Workspace hands it its own).
using EmitEvent = std::function<void(const WorkspaceEvent&)>;

// Whether it changes the graph as history keeps it (GraphState): the nodes, their settings, their links.
constexpr bool changesGraph(WorkspaceEventType t)
{
    switch (t) {
    case WorkspaceEventType::NodeAdded:
    case WorkspaceEventType::NodeRemoved:
    case WorkspaceEventType::NodeMoved:
    case WorkspaceEventType::NodeChanged:
    case WorkspaceEventType::LinkAdded:
    case WorkspaceEventType::LinkRemoved:
    case WorkspaceEventType::LayersChanged:
    case WorkspaceEventType::ArtboardsChanged:
        return true;
    default:
        return false;
    }
}
