#pragma once

#include <QWidget>
#include <QRectF>
#include <QPointF>
#include <QTransform>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QEvent>
#include <QPainter>
#include <QColor>
#include <QPen>

#include <vector>
#include <utility>
#include <algorithm>

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