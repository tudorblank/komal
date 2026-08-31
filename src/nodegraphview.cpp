#include "nodegraphview.hpp"

#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QGraphicsLineItem>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QWheelEvent>
#include <cmath>

// ==== NodeItem ====
QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if(change == ItemPositionChange || change == ItemPositionHasChanged)
        updateConnectedEdges();
    return QGraphicsItemGroup::itemChange(change, value);
}
void NodeItem::updateConnectedEdges()
{
    for(EdgeItem* edge : m_connectedEdges)
        edge->updatePath();
}

// ==== EdgeItem ====
void EdgeItem::updatePath()
{
    if(!m_from || !m_to) return;

    QPointF fromCenter = m_from->pos() + m_from->m_localRect.center();
    QPointF toCenter   = m_to->pos()   + m_to->m_localRect.center();

    QPainterPath path(fromCenter);
    float midX = (fromCenter.x() + toCenter.x()) / 2.0f;
    path.cubicTo(QPointF(midX, fromCenter.y()), QPointF(midX, toCenter.y()), toCenter);
    setPath(path);
}

// ==== NodeGraphView ====
NodeGraphView::NodeGraphView(QWidget* parent)
    : QGraphicsView(parent)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setBackgroundBrush(QColor(24, 24, 24));

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    constexpr float kWorldExtent = 15000.0f; // generous but finite, not the previous 100k
    setSceneRect(-kWorldExtent, -kWorldExtent, kWorldExtent * 2, kWorldExtent * 2);

    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);

    setFocusPolicy(Qt::StrongFocus); // so keyPressEvent (F key) actually reaches this widget

    // origin marker — small fixed crosshair at world (0,0)
    constexpr float kMarkerSize = 14.0f;
    QPen markerPen(QColor(90, 90, 100));
    markerPen.setWidth(0);
    auto* hLine = m_scene->addLine(-kMarkerSize, 0, kMarkerSize, 0, markerPen);
    auto* vLine = m_scene->addLine(0, -kMarkerSize, 0, kMarkerSize, markerPen);
    hLine->setZValue(-1); // above grid, below nodes/edges
    vLine->setZValue(-1);
}

void NodeGraphView::keyPressEvent(QKeyEvent* event)
{
    if(event->key() == Qt::Key_F)
    {
        frameAllNodes();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void NodeGraphView::frameAllNodes()
{
    if(m_nodeItems.empty()) return;

    QRectF bounds;
    bool first = true;
    for(auto& [id, node] : m_nodeItems)
    {
        QRectF nodeBounds(node->pos(), node->m_localRect.size());
        if(first) { bounds = nodeBounds; first = false; }
        else bounds = bounds.united(nodeBounds);
    }

    bounds = bounds.adjusted(-60, -60, 60, 60);
    fitInView(bounds, Qt::KeepAspectRatio);
}

void NodeGraphView::wheelEvent(QWheelEvent* event)
{
    float factor = (event->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);

    float currentScale = transform().m11();
    float newScale = currentScale * factor;
    if(newScale < 0.1f || newScale > 5.0f) return;

    scale(factor, factor);
}

void NodeGraphView::drawBackground(QPainter* painter, const QRectF& rect)
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

void NodeGraphView::setSnapshot(const GraphSnapshot& snapshot)
{
    // remove old nodes/edges only — leave the origin marker (added once in constructor) alone
    for(auto& [id, node] : m_nodeItems)
    {
        for(EdgeItem* edge : node->m_connectedEdges)
        {
            m_scene->removeItem(edge);
            delete edge;
        }
        node->m_connectedEdges.clear();
    }
    for(auto& [id, node] : m_nodeItems)
    {
        m_scene->removeItem(node);
        delete node;
    }
    m_nodeItems.clear();

    constexpr float kBoxW = 140.0f;
    constexpr float kBoxH = 50.0f;

    for(const GraphNodeDesc& n : snapshot.nodes)
    {
        auto* rect = new QGraphicsRectItem(0, 0, kBoxW, kBoxH);
        rect->setPen(QPen(QColor(90, 90, 90)));
        rect->setBrush(QBrush(QColor(45, 45, 45)));

        auto* text = new QGraphicsTextItem(n.label);
        text->setDefaultTextColor(Qt::white);
        QRectF tb = text->boundingRect();
        text->setPos((kBoxW - tb.width()) / 2.0f, (kBoxH - tb.height()) / 2.0f);

        auto* node = new NodeItem();
        node->m_localRect = QRectF(0, 0, kBoxW, kBoxH);
        node->addToGroup(rect);
        node->addToGroup(text);
        node->setPos(n.x, n.y);
        node->setFlag(QGraphicsItem::ItemIsMovable, true);
        node->setFlag(QGraphicsItem::ItemIsSelectable, true);
        node->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
        node->setZValue(1);

        m_scene->addItem(node);
        m_nodeItems[n.id] = node;
    }

    for(const GraphEdgeDesc& e : snapshot.edges)
    {
        auto fromIt = m_nodeItems.find(e.fromId);
        auto toIt   = m_nodeItems.find(e.toId);
        if(fromIt == m_nodeItems.end() || toIt == m_nodeItems.end()) continue;

        auto* edge = new EdgeItem();
        edge->setPen(QPen(QColor(120, 170, 220), 2));
        edge->setZValue(0);
        edge->m_from = fromIt->second;
        edge->m_to = toIt->second;

        fromIt->second->m_connectedEdges.push_back(edge);
        toIt->second->m_connectedEdges.push_back(edge);

        m_scene->addItem(edge);
        edge->updatePath();
    }

    frameAllNodes();
}