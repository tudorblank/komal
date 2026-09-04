#include "graphview-minimap.hpp"

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

    int w = std::clamp((int)(m_sceneBounds.width()  / 4.0f), kMinW, kMaxW);
    int h = std::clamp((int)(m_sceneBounds.height() / 4.0f), kMinH, kMaxH);

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

    float sx = target.width()  / m_sceneBounds.width();
    float sy = target.height() / m_sceneBounds.height();

    QTransform t;
    t.translate(target.left(), target.top());
    t.scale(sx, sy);
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