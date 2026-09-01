#pragma once

#include <QObject>
#include <QString>
#include <deque>
#include <unordered_map>
#include <memory>

#include "raster/raster-utils.hpp"
#include "raster/raster-base.hpp"
#include "node/node-base.hpp"
#include "node/node-compositor.hpp"
#include "shell/graphview.hpp"

class Project : public QObject
{
    Q_OBJECT
public:
    std::deque<RasterData> rawRasters;
    std::unordered_map<QString, std::shared_ptr<Node>> nodes;
    std::vector<std::pair<QString, QString>> edges;
    std::shared_ptr<CompositorNode> masterCompositor;

    void addNode(const QString& id, std::shared_ptr<Node> node, const QString& label, float x, float y);
    void addEdge(const QString& fromId, const QString& toId) { edges.push_back({fromId, toId}); }

    void setNodePosition(const QString& id, float x, float y);

    GraphSnapshot buildGraphSnapshot() const
    {
        GraphSnapshot snap;
        for(auto& [id, node] : nodes)
            snap.nodes.push_back({id, node->m_meta.label, node->m_meta.x, node->m_meta.y});
        for(auto& [fromId, toId] : edges)
            snap.edges.push_back({fromId, toId});
        return snap;
    }

signals:
    void nodeGraphChanged();
    void nodeMoved(QString id, float x, float y);
};