#pragma once

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItemGroup>
#include <QGraphicsPathItem>
#include <QKeyEvent>
#include <QString>
#include <vector>
#include <unordered_map>

struct GraphNodeDesc{
    QString id;
    QString label;
    float x, y;
};
struct GraphEdgeDesc{
    QString fromId;
    QString toId;
};
struct GraphSnapshot{
    std::vector<GraphNodeDesc> nodes;
    std::vector<GraphEdgeDesc> edges;
};

class EdgeItem;

class NodeItem : public QGraphicsItemGroup{
public:
    QRectF m_localRect;
    std::vector<EdgeItem*> m_connectedEdges;

    void updateConnectedEdges();

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
};

class EdgeItem : public QGraphicsPathItem{
public:
    NodeItem* m_from = nullptr;
    NodeItem* m_to = nullptr;

    void updatePath();
};

class NodeGraphView : public QGraphicsView{
    Q_OBJECT
public:
    explicit NodeGraphView(QWidget* parent = nullptr);
    void setSnapshot(const GraphSnapshot& snapshot);
    void frameAllNodes();

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QGraphicsScene* m_scene;
    std::unordered_map<QString, NodeItem*> m_nodeItems;
};