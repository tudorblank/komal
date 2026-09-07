#include "project.hpp"
#include <algorithm>

void Project::init()
{
    // master compositor
    m_masterCompositor = CompositorNode::create(CompositorType::Master);
    m_masterCompositor->enableCache(true);
    m_nodes.emplace(m_masterCompositor->m_meta.id, m_masterCompositor);

    // first raster
    m_rawRasters.emplace_back();
    auto raster0 = RasterRootNode::create(&m_rawRasters[0], ++m_rasterNodeCount);
    m_masterCompositor->addLayer(raster0);
    m_nodes.emplace(raster0->m_meta.id, raster0);
    m_activeRaster = raster0;

    // emit signal
}

// node utils
void Project::setNodePosition(const QString& id, float x, float y)
{
    auto it = m_nodes.find(id);
    if(it == m_nodes.end()) return;
    auto node = it->second;

    node->m_meta.x = x;
    node->m_meta.y = y;

    emit signalNodePosChanged(node->m_meta.id, x, y);
}
QString Project::duplicateNode(const QString& id, float x, float y)
{
    auto it = m_nodes.find(id);
    if(it == m_nodes.end()) return QString();

    auto ref = ReferenceNode::create(it->second);
    ref->m_meta.x = x;
    ref->m_meta.y = y;
    m_nodes.emplace(ref->m_meta.id, ref);

    emit signalNodeAdded(ref->m_meta.id);
    emit signalEdgeAdded(id, ref->m_meta.id);

    return ref->m_meta.id;
}
void Project::deleteNode(const QString& id)
{
    auto it = m_nodes.find(id);
    if(it == m_nodes.end()) return;
    auto node = it->second;

    auto inputs = node->getInputs();
    std::shared_ptr<Node> upstream = inputs.empty() ? nullptr : inputs[0];

    std::vector<std::shared_ptr<Node>> listeners;
    listeners.reserve(node->m_listeners.size());
    for(auto& weak : node->m_listeners)
        if(auto listener = weak.lock())
            listeners.push_back(listener);

    // remove pre-existing edges
    if(upstream) emit signalEdgeRemoved(upstream->m_meta.id, id);
    for(auto& listener : listeners)
        emit signalEdgeRemoved(id, listener->m_meta.id);

    // rewire + emit new edges
    for(auto& listener : listeners)
    {
        listener->replaceInputInstance(node, upstream);
        if(upstream) emit signalEdgeAdded(upstream->m_meta.id, listener->m_meta.id);
        if(listener == m_masterCompositor) emit signalMasterLayersChanged();
    }

    if(upstream) Node::unlisten(upstream, node);
    m_nodes.erase(it);
    emit signalNodeRemoved(id);
}
void Project::deleteNodes(const std::vector<QString>& ids)
{ for(const QString& id: ids) deleteNode(id); }

void Project::connectNodes(const QString& fromId, const QString& toId)
{
    if(fromId == toId) return;
    if(toId == m_masterCompositor->m_meta.id) return; // master takes layers via addNodeToMaster*, not this

    auto fromIt = m_nodes.find(fromId);
    auto toIt   = m_nodes.find(toId);
    if(fromIt == m_nodes.end() || toIt == m_nodes.end()) return;

    auto fromNode = fromIt->second;
    auto toNode   = toIt->second;

    auto currentInputs = toNode->getInputs();
    std::shared_ptr<Node> oldInput = currentInputs.empty() ? nullptr : currentInputs[0];
    if(oldInput == fromNode) return; // already wired

    toNode->replaceInputInstance(oldInput, fromNode);

    if(oldInput) emit signalEdgeRemoved(oldInput->m_meta.id, toId);
    emit signalEdgeAdded(fromId, toId);
}

// raster node
void Project::createRasterNode(float x, float y)
{
    m_rawRasters.emplace_back();
    auto raster = RasterRootNode::create(&m_rawRasters.back(), ++m_rasterNodeCount);
    raster->m_meta.x = x;
    raster->m_meta.y = y;
    m_nodes.emplace(raster->m_meta.id, raster);

    emit signalNodeAdded(raster->m_meta.id);
}
QString Project::createRasterNodeFromImage(const QString& path, float x, float y)
{
    m_rawRasters.emplace_back();
    RasterData& raster = m_rawRasters.back();

    if(!raster.loadImageFromPath(path.toUtf8().constData(), 0, 0))
    {
        m_rawRasters.pop_back();
        return QString();
    }

    auto rasterNode = RasterRootNode::create(&raster, ++m_rasterNodeCount);
    rasterNode->m_meta.x = x;
    rasterNode->m_meta.y = y;
    m_nodes.emplace(rasterNode->m_meta.id, rasterNode);

    emit signalNodeAdded(rasterNode->m_meta.id);
    return rasterNode->m_meta.id;
}
void Project::setActiveRaster(const QString& id)
{
    auto it = m_nodes.find(id);
    if(it == m_nodes.end()) return;
    if(it->second->m_meta.type != NodeType::Raster) return;

    m_activeRaster = std::static_pointer_cast<RasterRootNode>(it->second);

    emit signalActiveRasterChanged(it->second->m_meta.id);
}

// master compositor
size_t Project::findMasterLayerIndex(const QString& id) const
{
    auto& layers = m_masterCompositor->m_layers;
    for(size_t i = 0; i < layers.size(); i++)
        if(layers[i] && layers[i]->m_meta.id == id) return i;
    return SIZE_MAX; // not found sentinel
}
void Project::addNodeToMaster(const QString& id)
{
    auto it = m_nodes.find(id);
    if(it == m_nodes.end()) return;

    for(auto& layer : m_masterCompositor->m_layers)
        if(layer && layer->m_meta.id == id) return;
    
    m_masterCompositor->addLayer(it->second);
    
    emit signalMasterLayersChanged();
}
void Project::addNodeToMasterAt(const QString& id, size_t index)
{
    auto it = m_nodes.find(id);
    if(it == m_nodes.end()) return;
    for(auto& layer : m_masterCompositor->m_layers)
        if(layer && layer->m_meta.id == id) return;
    m_masterCompositor->addLayerAt(it->second, index);
    
    emit signalMasterLayersChanged();
}
void Project::removeMasterLayer(const QString& id)
{
    auto& layers = m_masterCompositor->m_layers;
    for(size_t i = 0; i < layers.size(); i++)
        if(layers[i] && layers[i]->m_meta.id == id) { m_masterCompositor->removeLayer(i); break; }
    
    emit signalMasterLayersChanged();
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
    
    emit signalMasterLayersChanged();
}

/*
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
*/