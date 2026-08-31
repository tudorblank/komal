#pragma once

#include <QMouseEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItemGroup>
#include <QGraphicsPathItem>
#include <QKeyEvent>
#include <QString>

#include <vector>
#include <unordered_map>
#include <algorithm>

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

class MiniMapWidget : public QWidget{
    Q_OBJECT
public:
    explicit MiniMapWidget(QWidget* parent = nullptr);

    void setNodeRects(const std::vector<QRectF>& rects);
    void setViewportRect(const QRectF& sceneRectVisible);
    void setEdges(const std::vector<std::pair<QPointF, QPointF>>& edges);
    
signals:
    void navigateRequested(QPointF scenePos);
    void nodeActivated(QPointF nodeCenterScenePos);
    void geometryUpdated();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    std::vector<QRectF> m_nodeRects;
    QRectF m_viewportRect;
    std::vector<std::pair<QPointF, QPointF>> m_edges;
    QRectF m_sceneBounds;
    int m_hoveredIndex = -1;

    static constexpr int kMaxW = 200;
    static constexpr int kMaxH = 150;
    static constexpr int kMinW = 60;
    static constexpr int kMinH = 45;

    QTransform sceneToWidgetTransform() const;
    void recomputeBounds();
    void recomputeSize();
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
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QGraphicsScene* m_scene;
    std::unordered_map<QString, NodeItem*> m_nodeItems;
    std::vector<EdgeItem*> m_edgeItems;
    MiniMapWidget* m_miniMap = nullptr;
    void updateMiniMap();
};