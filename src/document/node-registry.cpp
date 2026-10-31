#include "document/node-registry.hpp"
#include "node/node-kinds.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <utility>

Node* NodeRegistry::find(NodeId id) const
{
    auto it = m_nodes.find(id);
    return it == m_nodes.end() ? nullptr : it->second.get();
}

NodePtr NodeRegistry::findPtr(NodeId id) const
{
    auto it = m_nodes.find(id);
    return it == m_nodes.end() ? nullptr : it->second;
}

RasterRootNode& NodeRegistry::rasterNode(RasterId id) const
{
    assert(size_t(id) < m_rasterNodes.size() && "no such raster");
    return *m_rasterNodes[size_t(id)];
}

NodeId NodeRegistry::create(GraphKey key, NodeType type, float x, float y)
{
    const NodeKind& kind = nodeKind(type);
    RasterData* pixels = kind.ownsPixels ? &m_rasters.emplace_back(RasterId(m_rasters.size())) : nullptr;
    NodePtr n = kind.make(pixels);
    if (pixels) m_rasterNodes.push_back(std::static_pointer_cast<RasterRootNode>(n));   // RasterId == index
    const NodeId id = NodeId(m_nextId++);
    n->setId(key, id);
    n->setPosition(key, x, y);
    n->setLabel(key, kind.ownsPixels ? std::string(kind.label) + " " + std::to_string(++m_numbered) : std::string(kind.label));
    index(n);
    m_order.push_back(std::move(n));
    return id;
}

void NodeRegistry::remove(NodeId id)
{
    auto it = m_nodes.find(id);
    if (it == m_nodes.end()) return;
    const NodePtr node = std::move(it->second);
    m_nodes.erase(it);
    std::erase(m_order, node);
}

void NodeRegistry::restore(NodePtr n)
{
    const NodeId id = n->id();
    if (m_nodes.contains(id)) return;
    const auto at = std::lower_bound(m_order.begin(), m_order.end(), id, [](const NodePtr& o, NodeId v) { return o->id() < v; });
    index(n);
    m_order.insert(at, std::move(n));
}

NodeRegistry::PixelBytes NodeRegistry::pixelBytes() const
{
    PixelBytes b;
    for (const RasterData& r : m_rasters) {
        b.pixels  += r.bytes();
        b.history += r.history().bytes();
    }
    return b;
}

void NodeRegistry::index(const NodePtr& n)
{
    m_nodes.emplace(n->id(), n);
}
