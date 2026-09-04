// project.hpp
#pragma once
#include <QObject>
#include <QString>
#include <deque>
#include <unordered_map>
#include <memory>
#include <unordered_set>

#include "raster/raster-utils.hpp"
#include "raster/raster-base.hpp"
#include "node/node-base.hpp"
#include "node/node-compositor.hpp"
#include "gfx/blursys.hpp"
#include "shell/graphview.hpp"

class Project : public QObject
{
    Q_OBJECT
public:
    std::deque<RasterData> m_rawRasters;
    std::unordered_map<QString, std::shared_ptr<Node>> m_nodes;
    std::shared_ptr<CompositorNode> m_masterCompositor;
    std::shared_ptr<RasterRootNode> m_activeRaster;

    uint32_t m_rasterNodeCount = 0; // never decreases

    void init();

    void setNodePosition(const QString& id, float x, float y);
    void setActiveRaster(std::shared_ptr<RasterRootNode> layer) { m_activeRaster = layer; }

    void addNodeToMaster(const QString& id);
    void addNodeToMasterAt(const QString& id, size_t index);
    void removeMasterLayerByNodeId(const QString& id);
    void moveMasterLayer(const QString& id, size_t newIndex);

    GraphSnapshot buildGraphSnapshot() const;

    // nodes
    void createRasterNode(float x, float y);
    QString duplicateNode(const QString& id, float x, float y);

private:
    void addNode(std::shared_ptr<Node> node, float x, float y);
    void walkNode(const std::shared_ptr<Node>& node, GraphSnapshot& snap, std::unordered_set<Node*>& visited) const;

signals:
    void nodeGraphChanged();
};