#include "project.hpp"
#include <algorithm>

void Project::init()
{
    m_masterCompositor = CompositorNode::create(CompositorType::Master);
    m_masterCompositor->enableCache(true);

    m_rawRasters.emplace_back(); // initial layer 1
    auto raster0 = RasterRootNode::create(&m_rawRasters[0], ++m_rasterNodeCount);
    m_masterCompositor->addLayer(raster0);

    addNode(raster0, 0, 0);
    addNode(m_masterCompositor, 100, 0);

    setActiveRaster(raster0);

    emit nodeGraphChanged();

}

void Project::addNode(std::shared_ptr<Node> input, float x, float y)
{
    if(!input) return;
    if(m_nodes.contains(input->m_meta.id)) return;
    input->m_meta.x = x;
    input->m_meta.y = y;
    m_nodes[input->m_meta.id] = input;
}
void Project::setNodePosition(const QString& id, float x, float y)
{
    auto it = m_nodes.find(id);
    if(it == m_nodes.end()) return;
    it->second->m_meta.x = x;
    it->second->m_meta.y = y;
}

void Project::walkNode(const std::shared_ptr<Node>& node, GraphSnapshot& snap, std::unordered_set<Node*>& visited) const
{
    if(!node || !visited.insert(node.get()).second) return;

    snap.nodes.push_back({ node->m_meta.id, node->m_meta.label, node->m_meta.x, node->m_meta.y });

    for(auto& input : node->getInputs())
    {
        if(input) snap.edges.push_back({ input->m_meta.id, node->m_meta.id });
        walkNode(input, snap, visited);
    }
}

// nodes
GraphSnapshot Project::buildGraphSnapshot() const
{
    GraphSnapshot snap;
    std::unordered_set<Node*> visited;
    visited.insert(m_masterCompositor.get());

    for(auto& [id, node] : m_nodes)
    {
        if(!node || node.get() == m_masterCompositor.get()) continue;
        walkNode(node, snap, visited);
    }
    return snap;
}

void Project::addNodeToMaster(const QString& id)
{
    auto it = m_nodes.find(id);
    if(it == m_nodes.end()) return;
    for(auto& layer : m_masterCompositor->m_layers)
        if(layer && layer->m_meta.id == id) return;
    m_masterCompositor->addLayer(it->second);
    emit nodeGraphChanged();
}
void Project::addNodeToMasterAt(const QString& id, size_t index)
{
    auto it = m_nodes.find(id);
    if(it == m_nodes.end()) return;
    for(auto& layer : m_masterCompositor->m_layers)
        if(layer && layer->m_meta.id == id) return;
    m_masterCompositor->addLayerAt(it->second, index);
    emit nodeGraphChanged();
}
void Project::removeMasterLayerByNodeId(const QString& id)
{
    auto& layers = m_masterCompositor->m_layers;
    for(size_t i = 0; i < layers.size(); i++)
        if(layers[i] && layers[i]->m_meta.id == id) { m_masterCompositor->removeLayer(i); break; }
    emit nodeGraphChanged();
}
void Project::moveMasterLayer(const QString& id, size_t newIndex)
{
    auto& layers = m_masterCompositor->m_layers;
    for(size_t i = 0; i < layers.size(); i++)
        if(layers[i] && layers[i]->m_meta.id == id)
        {
            newIndex = std::min(newIndex, layers.size() - 1);
            m_masterCompositor->moveLayer(i, newIndex);
            break;
        }
    emit nodeGraphChanged();
}
// nodes
void Project::createRasterNode(float x, float y)
{
    m_rawRasters.emplace_back();
    auto raster = RasterRootNode::create(&m_rawRasters.back(), ++m_rasterNodeCount);
    m_masterCompositor->addLayer(raster);
    addNode(raster, x, y);
    emit nodeGraphChanged();
}
QString Project::duplicateNode(const QString& id, float x, float y)
{
    auto it = m_nodes.find(id);
    if(it == m_nodes.end()) return QString();

    auto ref = ReferenceNode::create(it->second);
    addNode(ref, x, y);
    emit nodeGraphChanged();
    return ref->m_meta.id;
}