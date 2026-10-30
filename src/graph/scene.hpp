#pragma once
// The graph as a scene of items, like QGraphicsScene: a NodeItem per node and a GraphLink per
// link, in graph space, built once and then changed only by what Project says changed:
//
//   NodeAdded      the item laid out, links into it added
//   NodeRemoved    the item and its links dropped
//   NodeMoved      the item shifted, its links' ends follow
//   LinkAdded      one link (a mask link also lays out its node: a row appears)
//   LinkRemoved    one link
//   LayersChanged  the stack laid out again, its layer links re-added
//   PreviewChanged nodes being dragged shown where they're going; a lifted link hidden (a stack
//                  row it fed closes up), until the edit ends
//   anything else  nothing: values and pixels are painted as they are
//
// The graph's own view state works the same way: opening a section, or a drag's gap in a stack,
// lays out that one item. Views (GraphPanel) read the items and links as they are, and compare
// revisions to know when what they derive from them (screen curves, the minimap) is stale.

#include "base/signal.hpp"
#include "document/edit-preview.hpp"
#include "graph/node-item/item.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <cstdint>
#include <optional>
#include <vector>

class Project;
struct ProjectEvent;

struct GraphLink {
    NodeId    from, to;
    InputSlot slot;
    bool      mask;
    bool      toMaster;           // drawn dashed, fading out
    ImVec2    fromGraph, toGraph; // port positions, each in its node's own space
    bool      shown = true;       // both ports are there (a lifted layer's isn't)
};

class GraphScene {
public:
    using Items = absl::flat_hash_map<NodeId, NodeItem>;

    void init(const Project& project);   // items for what's there, then follows project.changed
    void shutdown();

    const Items& items() const { return m_items; }
    const NodeItem* item(NodeId id) const;
    const std::vector<NodeId>&    zOrder() const { return m_zOrder; }   // back to front
    const std::vector<GraphLink>& links()  const { return m_links; }
    bool isMaster(NodeId id) const;
    uint64_t itemsRevision() const { return m_itemsRevision; }   // bumped whenever an item changes
    uint64_t linksRevision() const { return m_linksRevision; }   // ... whenever a link or one of its ends does

    // The graph's own view state: not the document's, never undone. (Which nodes are selected is
    // the project's: the canvas shows it too.)
    void bringToFront(NodeId id);
    void toggleSection(NodeId id, NodeSignal signal);   // a signal's section opened or closed
    // The gap a drag opens in a stack: compositor `node` laid out with `layout` (kNoNodeId: none).
    // One at a time; the stack it leaves closes again.
    void setStackGap(NodeId node, StackLayout layout);

private:
    void onProjectEvent(const ProjectEvent& e);
    void followPreview();                 // the graph's part of the edit in progress
    ImVec2 position(NodeId id) const;     // where its item is: the node's place, or where it's being dragged
    bool lifted(const GraphLink& l) const { return m_lifted && l.to == m_lifted->from && l.slot == m_lifted->slot; }
    void addItem(NodeId id, bool withLinks = true);
    void removeItem(NodeId id);
    void moveItem(NodeId id);
    void layoutItem(NodeId id);   // after anything its layout depends on changed
    void addLink(NodeId from, NodeId to, const InputSlot& slot);
    void removeLink(NodeId to, const InputSlot& slot);
    void addLinksInto(NodeId to, bool layersOnly = false);   // what the project wires into it
    void placeLinkEnds(GraphLink& l) const;
    void placeLinkEndsOf(NodeId id);   // its ports moved
    StackLayout stackLayout(NodeId id) const;   // the gap, if it's in that stack

    const Project* m_project = nullptr;
    Connection     m_projectEvents;   // project.changed
    absl::flat_hash_map<NodeId, NodeItemState> m_viewStates;
    Items                  m_items;
    std::vector<NodeId>    m_zOrder;   // back to front
    std::vector<GraphLink> m_links;
    NodeId      m_gapNode = kNoNodeId;
    StackLayout m_gap;
    absl::flat_hash_set<NodeId> m_moving;   // being dragged (Project::preview()) ...
    ImVec2      m_movedBy;                  // ... this far
    std::optional<LinkMoveEdit> m_lifted;   // a link being carried
    uint64_t    m_itemsRevision = 1;
    uint64_t    m_linksRevision = 1;
};
