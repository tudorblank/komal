#pragma once
// The project's nodes: by id and in creation order (ids only grow: that's id order), and each
// raster node by its pixel data. It owns every raster's data (RasterData), which outlives the
// node: history may bring a deleted raster back, as itself, pixels and their undo included.
//
//   NodeId id = nodes.create(key, NodeType::Move, x, y);   // a new id, never reused
//   nodes.remove(id);                                 // gone (history keeps the node alive)
//   nodes.restore(node);                              // back as itself, in its place

#include "node/node-types.hpp"
#include "raster/raster-data.hpp"

#include <absl/container/flat_hash_map.h>
#include <deque>
#include <vector>

class NodeRegistry {
public:
    const std::vector<NodePtr>& nodes() const { return m_order; }   // creation order
    Node*   find(NodeId id) const;
    NodePtr findPtr(NodeId id) const;
    bool    contains(NodeId id) const { return m_nodes.contains(id); }
    RasterRootNode* rasterNode(RasterId id) const;   // null while its node is deleted
    RasterData&     raster(RasterId id) { return m_rasters[size_t(id)]; }

    // A new node of that kind (node-kinds.hpp) at (x, y) in the graph, named after its kind; a
    // kind that owns pixels gets its own data and a number ("Raster 3", never reused).
    NodeId create(GraphKey key, NodeType type, float x, float y);
    void   remove(NodeId id);
    void   restore(NodePtr n);                 // a removed node back, with its id

private:
    void index(const NodePtr& n);   // by id, and a raster by its data

    std::deque<RasterData> m_rasters;   // first, destroyed last: raster nodes point into it. RasterId == index
    std::vector<NodePtr> m_order;
    absl::flat_hash_map<NodeId, NodePtr> m_nodes;
    absl::flat_hash_map<RasterId, NodeId> m_rasterNodes;
    uint32_t m_nextId = 1;     // the next node's id
    uint32_t m_numbered = 0;   // nodes named with a number so far
};
