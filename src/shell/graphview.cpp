#include "graphview.hpp"

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

// ==== MiniMapWidget ====
MiniMapWidget::MiniMapWidget(QWidget* parent) : QWidget(parent)
{ setMouseTracking(true); }

void MiniMapWidget::setNodeRects(const std::vector<QRectF>& rects)
{
    m_nodeRects = rects;
    recomputeBounds();
    recomputeSize();
    update();
}
void MiniMapWidget::setViewportRect(const QRectF& sceneRectVisible)
{
    m_viewportRect = sceneRectVisible;
    recomputeBounds();
    recomputeSize();
    update();
}

void MiniMapWidget::recomputeBounds()
{
    QRectF bounds;
    bool first = true;
    for(const QRectF& r : m_nodeRects)
    {
        if(first) { bounds = r; first = false; }
        else bounds = bounds.united(r);
    }
    if(first)
        bounds = m_viewportRect.isEmpty() ? QRectF(-200, -200, 400, 400) : m_viewportRect;

    m_sceneBounds = bounds.adjusted(-60, -60, 60, 60);
}

void MiniMapWidget::recomputeSize()
{
    if(m_sceneBounds.isEmpty()) return;

    float aspect = m_sceneBounds.width() / m_sceneBounds.height();

    int w, h;
    if(aspect >= 1.0f) // wider than tall => fit to max width
    {
        w = kMaxW;
        h = (int)(kMaxW / aspect);
    }
    else // taller than wide => fit to max height
    {
        h = kMaxH;
        w = (int)(kMaxH * aspect);
    }

    w = std::clamp(w, kMinW, kMaxW);
    h = std::clamp(h, kMinH, kMaxH);

    if(w != width() || h != height())
    {
        setFixedSize(w, h);
        emit geometryUpdated(); // size changed
    }
}

void MiniMapWidget::setEdges(const std::vector<std::pair<QPointF, QPointF>>& edges)
{
    m_edges = edges;
    update();
}

QTransform MiniMapWidget::sceneToWidgetTransform() const
{
    QRectF target = rect().adjusted(4, 4, -4, -4);
    if(m_sceneBounds.isEmpty() || target.isEmpty()) return QTransform();

    float s = target.width() / m_sceneBounds.width();

    QTransform t;
    t.translate(target.left(), target.top());
    t.scale(s, s);
    t.translate(-m_sceneBounds.left(), -m_sceneBounds.top());
    return t;
}

void MiniMapWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.fillRect(rect(), QColor(18, 18, 20, 200));

    QTransform t = sceneToWidgetTransform();

    // edges first, under the node blocks
    painter.setPen(QPen(QColor(90, 110, 130), 1));
    for(const auto& [from, to] : m_edges)
        painter.drawLine(t.map(from), t.map(to));

    for(int i = 0; i < (int)m_nodeRects.size(); i++)
    {
        QRectF wr = t.mapRect(m_nodeRects[i]);
        bool hovered = (i == m_hoveredIndex);

        painter.setPen(Qt::NoPen);
        painter.setBrush(hovered ? QColor(140, 190, 240) : QColor(90, 90, 100));
        painter.drawRoundedRect(wr, 1.5, 1.5);
    }

    if(!m_viewportRect.isEmpty())
    {
        painter.setPen(QPen(QColor(120, 170, 220), 1.5));
        painter.setBrush(Qt::NoBrush); // outline only
        painter.drawRect(t.mapRect(m_viewportRect));
    }
}

void MiniMapWidget::mouseMoveEvent(QMouseEvent* event)
{
    QTransform t = sceneToWidgetTransform();
    QPointF pos = event->pos();

    int newHover = -1;
    for(int i = 0; i < (int)m_nodeRects.size(); i++)
        if(t.mapRect(m_nodeRects[i]).contains(pos)) { newHover = i; break; }
    if(newHover != m_hoveredIndex) { m_hoveredIndex = newHover; update(); }

    if(event->buttons() & Qt::LeftButton)
    {
        bool invertible = false;
        QTransform inv = t.inverted(&invertible);
        if(invertible) emit navigateRequested(inv.map(pos));
    }
}
void MiniMapWidget::leaveEvent(QEvent*)
{
    if(m_hoveredIndex != -1) { m_hoveredIndex = -1; update(); }
}
void MiniMapWidget::mousePressEvent(QMouseEvent* event)
{
    QTransform t = sceneToWidgetTransform();
    QPointF pos = event->pos();

    for(const QRectF& r : m_nodeRects)
        if(t.mapRect(r).contains(pos)) { emit nodeActivated(r.center()); return; }

    bool invertible = false;
    QTransform inv = t.inverted(&invertible);
    if(invertible) emit navigateRequested(inv.map(pos));
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

    m_miniMap = new MiniMapWidget(this);
    connect(m_miniMap, &MiniMapWidget::navigateRequested, this, [this](QPointF scenePos)
    {
        centerOn(scenePos);
        updateMiniMap();
    });
    connect(m_miniMap, &MiniMapWidget::nodeActivated, this, [this](QPointF nodeCenter)
    {
        centerOn(nodeCenter);
        updateMiniMap();
    });
    connect(m_miniMap, &MiniMapWidget::geometryUpdated, this, [this]()
    {
        constexpr int kMargin = 12;
        m_miniMap->move(width() - m_miniMap->width() - kMargin,
                        height() - m_miniMap->height() - kMargin);
    });

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    constexpr float kWorldExtent = 15000.0f;
    setSceneRect(-kWorldExtent, -kWorldExtent, kWorldExtent * 2, kWorldExtent * 2);

    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);

    setFocusPolicy(Qt::StrongFocus); // ensure keyPressEvent (F key) reaches this widget

    // origin marker
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
void NodeGraphView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if(m_miniMap)
    {
        constexpr int kMargin = 12;
        m_miniMap->move(width() - m_miniMap->width() - kMargin,
                         height() - m_miniMap->height() - kMargin);
    }
}
void NodeGraphView::paintEvent(QPaintEvent* event)
{
    QGraphicsView::paintEvent(event);
    updateMiniMap();
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

    updateMiniMap();
}

void NodeGraphView::wheelEvent(QWheelEvent* event)
{
    float factor = (event->angleDelta().y() > 0) ? 1.15f : (1.0f / 1.15f);

    float currentScale = transform().m11();
    float newScale = currentScale * factor;
    if(newScale < 0.1f || newScale > 5.0f) return;

    scale(factor, factor);

    updateMiniMap();
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
    auto* edge = new EdgeItem();

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
    m_edgeItems.clear();

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
        m_edgeItems.push_back(edge);
    }

    frameAllNodes();

    updateMiniMap();
}

void NodeGraphView::updateMiniMap()
{
    if(!m_miniMap) return;

    std::vector<QRectF> rects;
    rects.reserve(m_nodeItems.size());
    for(auto& [id, node] : m_nodeItems)
        rects.push_back(QRectF(node->pos(), node->m_localRect.size()));

    std::vector<std::pair<QPointF, QPointF>> edgePairs;
    edgePairs.reserve(m_edgeItems.size());
    for(EdgeItem* edge : m_edgeItems)
    {
        if(!edge->m_from || !edge->m_to) continue;
        QPointF fromCenter = edge->m_from->pos() + edge->m_from->m_localRect.center();
        QPointF toCenter   = edge->m_to->pos()   + edge->m_to->m_localRect.center();
        edgePairs.emplace_back(fromCenter, toCenter);
    }

    m_miniMap->setNodeRects(rects);
    m_miniMap->setEdges(edgePairs);
    m_miniMap->setViewportRect(mapToScene(viewport()->rect()).boundingRect());
}