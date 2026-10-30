#pragma once
// What changed, told to every listener right after the change is made (the project's state is
// final by then). Views keep their own items and update them from these; nothing re-reads the
// whole project. Pixels travel separately, as dirty tiles through the node graph.

#include "document/input-slot.hpp"

#include <functional>

enum class ProjectEventType : uint8_t {
    NodeAdded,             // node
    NodeRemoved,           // node (its links are removed first)
    NodeMoved,             // node: its editor position
    NodeChanged,           // node: a value (signal scale, move offset, label, raster locks)
    LinkAdded,             // source feeds node at slot: a single input (Input, Mask)
    LinkRemoved,           // source no longer feeds node at slot
    LayersChanged,         // node: a stack's layers were added, removed, replaced or reordered
    PreviewChanged,        // an interactive edit started, moved, was applied or cancelled
    ActiveRasterChanged,
    RasterEdited,          // pixels of `node` changed (edit, undo, redo)
    HistoryChanged,
    SelectionChanged,      // which nodes are selected (Project::selection())
    CanvasViewChanged,     // where the canvas looks, or its size (Project::canvasCamera(), canvasViewport())
};

struct ProjectEvent {
    ProjectEventType type;
    NodeId    node   = kNoNodeId;
    NodeId    source = kNoNodeId;   // Link*
    InputSlot slot;                 // Link*
};

// How a block that changes the graph tells (Project hands it its own).
using EmitEvent = std::function<void(const ProjectEvent&)>;

// Whether it changes the graph as history keeps it (GraphState): the nodes, their settings, their links.
constexpr bool changesGraph(ProjectEventType t)
{
    switch (t) {
    case ProjectEventType::NodeAdded:
    case ProjectEventType::NodeRemoved:
    case ProjectEventType::NodeMoved:
    case ProjectEventType::NodeChanged:
    case ProjectEventType::LinkAdded:
    case ProjectEventType::LinkRemoved:
    case ProjectEventType::LayersChanged:
        return true;
    default:
        return false;
    }
}
