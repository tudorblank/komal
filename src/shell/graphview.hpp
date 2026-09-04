#pragma once

#include <QMouseEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QShowEvent>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItemGroup>
#include <QGraphicsPathItem>
#include <QKeyEvent>
#include <QString>
#include <QVariant>
#include <QRectF>
#include <QPainter>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QGraphicsLineItem>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QWheelEvent>

#include "graphview-minimap.hpp"
#include "graphview-masterwidget.hpp"
class Project;

#include <memory>
#include <vector>
#include <unordered_map>
#include <cmath>

// GRAPH SNAPSHOT

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

// Q NODE-GRAPH ITEMMS

class EdgeItem;

class NodeItem : public QGraphicsItemGroup{
public:
    QString m_id;
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

// Q NODE-GRAPH

class NodeGraphView : public QGraphicsView{
    Q_OBJECT
public:
    explicit NodeGraphView(std::shared_ptr<Project> project, QWidget* parent = nullptr);

    struct ConnectDrag{
        bool active = false;
        QString sourceNodeId;
        QPointF currentScreenPos;
    };
    ConnectDrag m_connectDrag;

private:
    std::shared_ptr<Project> m_project;
    QGraphicsScene* m_scene;
    std::unordered_map<QString, NodeItem*> m_nodeItems;
    std::vector<EdgeItem*> m_edgeItems;
    MiniMapWidget* m_miniMap = nullptr;
    MasterLayerStackWidget* m_layerStack = nullptr;
    std::vector<std::pair<QString, QPainterPath>> m_connectorPaths;

    // snapshot
    void refreshFromProject();
    void setSnapshot(const GraphSnapshot& snapshot);

    void frameAllNodes();
    void updateMiniMap();

    void refreshLayerStackRows();
    void duplicateSelectedNode();

protected:
    //// events

    // window
    void showEvent(QShowEvent* event) override
    {
        QGraphicsView::showEvent(event);
        frameAllNodes();
    }
    void resizeEvent(QResizeEvent* event) override
    {
        QGraphicsView::resizeEvent(event);
        constexpr int kMargin = 12;
        if(m_miniMap)
            m_miniMap->move(width() - m_miniMap->width() - kMargin,
                            height() - m_miniMap->height() - kMargin);
        if(m_layerStack)
            m_layerStack->move(width() - m_layerStack->width() - kMargin, kMargin);
    }
    void paintEvent(QPaintEvent* event) override;

    // mouse
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event)
    {
        float factor = (event->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);

        float currentScale = transform().m11();
        float newScale = currentScale * factor;
        if(newScale < 0.1f || newScale > 5.0f) return;

        scale(factor, factor);

        updateMiniMap();
    }

    // keyboard
    void keyPressEvent(QKeyEvent* event) override
    {
        if(event->key() == Qt::Key_F)
        {
            frameAllNodes();
            return;
        }
        if(event->key() == Qt::Key_D && (event->modifiers() & Qt::ControlModifier))
        {
            duplicateSelectedNode();
            return;
        }
        QGraphicsView::keyPressEvent(event);
    }

    // draw
    void drawBackground(QPainter* painter, const QRectF& rect) override
    {
        QGraphicsView::drawBackground(painter, rect);

        constexpr float kGridStep = 40.0f;

        QPen minorPen(QColor(32, 32, 32));
        minorPen.setWidth(0);
        painter->setPen(minorPen);

        float left = std::floor(rect.left() / kGridStep) * kGridStep;
        float top  = std::floor(rect.top()  / kGridStep) * kGridStep;

        for(float x = left; x < rect.right(); x += kGridStep)
            painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        for(float y = top; y < rect.bottom(); y += kGridStep)
            painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    }
    QPainterPath drawConnectorPath(QPainter& p, QPointF origin, QPointF target)
    {
        QLinearGradient grad(origin, target);
        grad.setColorAt(0.0, QColor(120, 170, 220, 255));
        grad.setColorAt(1.0, QColor(120, 170, 220, 120));

        QPen pen(QBrush(grad), 2.5, Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        float midX = (origin.x() + target.x()) / 2.0f;
        QPainterPath path(origin);
        path.cubicTo(QPointF(midX, origin.y()), QPointF(midX, target.y()), target);
        p.drawPath(path);
        return path;
    }
    void drawDragPreview(QPainter& p, QPointF origin, QPointF cursor)
    {
        QLinearGradient grad(origin, cursor);
        grad.setColorAt(0.0, QColor(120, 170, 220, 200));
        grad.setColorAt(1.0, QColor(120, 170, 220, 80));
        QPen pen(QBrush(grad), 2, Qt::DashLine);
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
        p.drawLine(origin, cursor);
    }
};