#pragma once
// The workspace's nodes: by id and in creation order (ids only grow: that's id order), and each
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
    // The node of a raster's data, deleted or not: its pixels change only through it, so a
    // history jump that flips a deleted raster leaves its caches right for when it comes back.
    RasterRootNode& rasterNode(RasterId id) const;

    // A new node of that kind (node-kinds.hpp) at (x, y) in the graph, named after its kind; a
    // kind that owns pixels gets its own data and a number ("Raster 3", never reused).
    NodeId create(GraphKey key, NodeType type, float x, float y);
    // A saved workspace's node, with its own id (above every id so far: kNoNodeId if not); its
    // name is the file's to give.
    NodeId create(GraphKey key, NodeType type, float x, float y, NodeId id);
    void   remove(NodeId id);
    void   restore(NodePtr n);                 // a removed node back, with its id

    // What every raster's pixels hold (in the graph or not), and their history's tiles.
    struct PixelBytes { size_t pixels = 0, history = 0; };
    PixelBytes pixelBytes() const;

    // What keeps ids and names from coming back: saved with a workspace, and picked up from there
    // (never backwards).
    uint32_t nextId() const   { return m_nextId; }
    uint32_t numbered() const { return m_numbered; }
    void resume(uint32_t nextId, uint32_t numbered);

private:
    void index(const NodePtr& n);   // by id
    NodePtr make(GraphKey key, NodeType type, float x, float y, NodeId id);   // made, placed, indexed

    std::deque<RasterData> m_rasters;   // first, destroyed last: raster nodes point into it. RasterId == index
    std::vector<std::shared_ptr<RasterRootNode>> m_rasterNodes;   // by RasterId, whether in the graph or not
    std::vector<NodePtr> m_order;
    absl::flat_hash_map<NodeId, NodePtr> m_nodes;
    uint32_t m_nextId = 1;     // the next node's id
    uint32_t m_numbered = 0;   // nodes named with a number so far
};
