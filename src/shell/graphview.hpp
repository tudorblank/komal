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
#include <QRubberBand>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QUrl>
#include <QGraphicsPixmapItem>
#include <QFont>
#include <QTimer>
#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QGraphicsEllipseItem>
#include <QScrollBar>
#include <QDir>
#include <QDebug>
#include <QFontDatabase>

#include "graphview-minimap.hpp"
#include "graphview-masterwidget.hpp"
class Project;
class Node;

#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <cmath>

// Q "GRAPH ITEMS"

class EdgeItem;

class NodeItem : public QGraphicsItemGroup{
public:
    QString m_id;

    QRectF m_localRect;
    QRectF m_thumbnailRect;
    QGraphicsPixmapItem* m_thumbnailItem = nullptr;

    QGraphicsEllipseItem* m_inputPort = nullptr;
    QGraphicsEllipseItem* m_outputPort = nullptr;
    QGraphicsRectItem* m_selectionOutline = nullptr;

    std::vector<EdgeItem*> m_connectedEdges;

    QPointF outputPortLocal() const { return { m_localRect.right(), m_localRect.center().y() }; }
    QPointF inputPortLocal()  const { return { m_localRect.left(),  m_localRect.center().y() }; }
    QPointF outputPortScene() const { return pos() + outputPortLocal(); }
    QPointF inputPortScene()  const { return pos() + inputPortLocal(); }

    void updateConnectedEdges();
protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

class EdgeItem : public QGraphicsPathItem{
public:
    NodeItem* m_from = nullptr;
    NodeItem* m_to = nullptr;

    void updatePath();
};

// Q "NODE GRAPH"

class NodeGraphView : public QGraphicsView{
    Q_OBJECT
public:
    explicit NodeGraphView(std::shared_ptr<Project> project, QWidget* parent = nullptr);
    ~NodeGraphView() {}

    struct ConnectDrag{
        bool active = false;
        QString sourceNodeId;
        QPointF currentScreenPos;
        QString hoverInputNodeId;
    };
    ConnectDrag m_connectDrag;

private:
    // project
    std::shared_ptr<Project> m_project;
    // scene
    QGraphicsScene* m_scene;

    // rubber band select
    bool m_ctrlClickPending = false;
    bool m_plainClickPending = false;
    bool m_rubberBandAdditive = false;
    bool m_spaceHeld = false;
    bool m_panning = false;
    QPoint m_lastPanPos;
    bool m_rubberBandSelecting = false;
    QPoint m_rubberBandOrigin;
    QRubberBand* m_rubberBand = nullptr;
    std::unordered_set<QString> m_rubberBandBaseSelected;

    // graph item maps
    std::unordered_map<QString, NodeItem*> m_nodeItems;
    std::unordered_map<QString, EdgeItem*> m_edgeItems;
    static QString edgeKey(const QString& fromID, const QString& toID)
    { return fromID + QStringLiteral("->") + toID; }

    MiniMapWidget* m_miniMap = nullptr;
    MasterCompositorWidget* m_masterWidget = nullptr;
    std::vector<std::pair<QString, QPainterPath>> m_connectorPaths;

    // snapshot
    void refreshFromProject();

    // node thumbnails
    std::deque<QString> m_dirtyThumbnailQueue;
    std::unordered_set<QString> m_dirtyThumbnailSet;
    QTimer* m_thumbnailTimer = nullptr;
    QElapsedTimer m_thumbnailClock;
    std::unordered_map<QString, qint64> m_lastThumbnailRefreshMs;
    static constexpr qint64 kThumbnailMinIntervalMs = 150;
    static QImage renderNodeThumbnail(Node& node, int outW, int outH);
    void refreshNodeThumbnail(const QString& id);
    void drainThumbnailUpdates();

    // graph funcs
    void frameAllNodes();
    void duplicateSelectedNode();
    void deleteSelectedNodes();
    void updateRubberBandSelection();

    // minimap, master widget
    void updateMiniMap();
    void refreshMasterWidget();

    // signaled funcs
    void createNodeItem(const QString& id);
    void removeNodeItem(const QString& id);
    void createEdgeItem(const QString& fromID, const QString& toID);
    void removeEdgeItem(const QString& fromID, const QString& toID);

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
        if(m_masterWidget)
            m_masterWidget->move(width() - m_masterWidget->width() - kMargin, kMargin);
    }

    // mouse
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

    // keyboard
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

    // drag+drop
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    // draw
    void paintEvent(QPaintEvent* event) override;
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