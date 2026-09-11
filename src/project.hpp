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

class Project : public QObject
{
    Q_OBJECT
public:
    ~Project() { m_rawRasters.clear(); }
    
    //// members
    std::deque<RasterData> m_rawRasters;
    uint32_t m_rasterNodeCount = 0; // never decreases
    std::unordered_map<QString, std::shared_ptr<Node>> m_nodes;
    std::shared_ptr<CompositorNode> m_masterCompositor;
    std::shared_ptr<RasterRootNode> m_activeRaster;

    //// functions
    void init();
    // node utils
    void setNodePosition(const QString& id, float x, float y);
    QString duplicateNode(const QString& id, float x, float y); // ref node
    void deleteNode(const QString& id);
    void deleteNodes(const std::vector<QString>& ids);
    void connectNodes(const QString& fromId, const QString& toId);
    // raster node
    void createRasterNode(float x, float y);
    QString createRasterNodeFromImage(const QString& path, float x, float y);
    void setActiveRaster(const QString& id);
    // master compositor
    size_t findMasterLayerIndex(const QString& id) const;
    void addNodeToMaster(const QString& id);
    void addNodeToMasterAt(const QString& id, size_t index);
    void removeMasterLayer(const QString& id);
    void moveMasterLayer(const QString& id, size_t newIndex);

signals:
    void signalNodePosChanged(const QString& id, float x, float y);
    
    void signalNodeAdded(const QString& id);
    void signalNodeRemoved(const QString& id);
    
    void signalEdgeAdded(const QString& fromID, const QString& toID);
    void signalEdgeRemoved(const QString& fromID, const QString& toID);
    
    void signalActiveRasterChanged(const QString& id);
    
    void signalMasterLayersChanged();
};