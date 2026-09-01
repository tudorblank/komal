#include "project.hpp"

void Project::addNode(const QString& id, std::shared_ptr<Node> node, const QString& label, float x, float y)
{
    node->m_meta.label = label;
    node->m_meta.x = x;
    node->m_meta.y = y;
    nodes[id] = std::move(node);
}
void Project::setNodePosition(const QString& id, float x, float y)
{
    auto it = nodes.find(id);
    if(it == nodes.end()) return;
    it->second->m_meta.x = x;
    it->second->m_meta.y = y;
    emit nodeMoved(id, x, y);
}