#include "graphview-masterwidget.hpp"
#include "project.hpp"
#include <QPainter>
#include <QMouseEvent>
#include <cmath>

MasterLayerStackWidget::MasterLayerStackWidget(QWidget* parent) : QWidget(parent)
{ setMouseTracking(true); }

void MasterLayerStackWidget::setRows(std::vector<Row> rows)
{
    m_rows = std::move(rows);
    recomputeSize();
    update();
}

void MasterLayerStackWidget::recomputeSize()
{
    int rowCount = (int)m_rows.size() + (m_ghostActive ? 1 : 0) - (m_dragRowIndex >= 0 ? 1 : 0);
    int h = kRowH * rowCount + 30;
    if(h != height() || kWidth != width())
    {
        setFixedSize(kWidth, std::max(h, kRowH + 30));
        emit geometryUpdated();
    }
}

QRectF MasterLayerStackWidget::rowGeometry(int index) const
{
    int visualIndex = index;
    if(m_dragRowIndex >= 0 && index > m_dragRowIndex)
        visualIndex -= 1;
    if(m_ghostActive && m_ghostInsertIndex >= 0 && visualIndex >= m_ghostInsertIndex)
        visualIndex += 1;
    return QRectF(kStubMargin, 26 + visualIndex * kRowH, kRowBoxWidth, kRowH - 4);
}

QPointF MasterLayerStackWidget::portPosFor(int rowIndex) const
{
    QRectF r = rowGeometry(rowIndex);
    return QPointF(r.left(), r.center().y());
}

void MasterLayerStackWidget::updateGhostPosition(QPoint pos, bool isNewLayer)
{
    int slotCount = (int)m_rows.size() - (m_dragRowIndex >= 0 ? 1 : 0);
    int newIndex = slotCount;
    for(int i = 0; i < slotCount; i++)
    {
        float midY = 26 + i * kRowH + kRowH / 2.0f;
        if(pos.y() < midY) { newIndex = i; break; }
    }
    bool wasActive = m_ghostActive;
    m_ghostActive = true;
    if(newIndex != m_ghostInsertIndex || !wasActive)
    {
        m_ghostInsertIndex = newIndex;
        recomputeSize();
        update();
    }
}
void MasterLayerStackWidget::clearGhost()
{
    bool wasActive = m_ghostActive;
    m_ghostActive = false;
    m_ghostInsertIndex = -1;
    if(wasActive) recomputeSize();
    update();
}

void MasterLayerStackWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    constexpr int kPanelPad = 8;
    QRectF panelRect(kStubMargin - kPanelPad, 0, kRowBoxWidth + kPanelPad * 2, height());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(18, 18, 20, 220));
    p.drawRoundedRect(panelRect, 6, 6);

    p.setPen(Qt::white);
    p.drawText(panelRect.adjusted(6, 4, -6, 0), Qt::AlignLeft, "MASTER");

    for(int i = 0; i < (int)m_rows.size(); i++)
    {
        if(i == m_dragRowIndex) continue;

        QRectF r = rowGeometry(i);
        p.setPen(QPen(QColor(90, 90, 90)));
        p.setBrush(QColor(45, 45, 45));
        p.drawRoundedRect(r, 3, 3);

       p.setPen(Qt::white);
        p.drawText(r.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, m_rows[i].label);
    }

    if(m_ghostActive && m_ghostInsertIndex >= 0)
    {
        QRectF gr(kStubMargin, 26 + m_ghostInsertIndex * kRowH, kRowBoxWidth, kRowH - 4);
        QPen dash(QColor(120, 170, 220));
        dash.setStyle(Qt::DashLine);
        p.setPen(dash);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(gr, 3, 3);
    }
}

bool MasterLayerStackWidget::pointNearSegment(QPointF p, QPointF a, QPointF b, qreal tol)
{
    QPointF ab = b - a;
    qreal lenSq = QPointF::dotProduct(ab, ab);
    qreal t = lenSq > 0.0001 ? qBound(0.0, QPointF::dotProduct(p - a, ab) / lenSq, 1.0) : 0.0;
    QPointF proj = a + ab * t;
    return QLineF(p, proj).length() < tol;
}

QPointF MasterLayerStackWidget::stubEndPoint(QPointF portPt)
{
    QLineF dir(0, 0, kStubLength, 0);
    dir.setAngle(200.0);
    dir.translate(portPt);
    return dir.p2();
}

QPainterPath MasterLayerStackWidget::stubPath(QPointF portPt)
{
    QPointF end = stubEndPoint(portPt);
    QPointF delta = end - portPt;

    QPointF perp(-delta.y(), delta.x());
    qreal perpLen = std::sqrt(QPointF::dotProduct(perp, perp));
    if(perpLen > 0.0001) perp /= perpLen;

    QPointF c1 = portPt + delta * 0.33 + perp * kStubArch;
    QPointF c2 = portPt + delta * 0.66 - perp * kStubArch;

    QPainterPath path(portPt);
    path.cubicTo(c1, c2, end);
    return path;
}

void MasterLayerStackWidget::mousePressEvent(QMouseEvent* event)
{
    for(int i = 0; i < (int)m_rows.size(); i++)
    {
        QPointF portPt = portPosFor(i);
        if(pointNearSegment(event->pos(), portPt, stubEndPoint(portPt), kPortGrabTolerance))
        {
            m_pendingPressRow = i;
            m_pressPos = event->pos();
            return;
        }
    }
}

void MasterLayerStackWidget::mouseMoveEvent(QMouseEvent* event)
{
    if(m_dragRowIndex >= 0)
    {
        bool insidePanel = rect().contains(event->pos());
        QPoint globalPos = event->globalPosition().toPoint();

        if(insidePanel)
        {
            if(m_dragIsOutside)
            {
                emit rowDragEnded(globalPos);
                m_dragIsOutside = false;
            }
            updateGhostPosition(event->pos(), false);
        }
        else
        {
            if(!m_dragIsOutside)
            {
                clearGhost();
                m_dragIsOutside = true;
                emit rowDragStarted(m_rows[m_dragRowIndex].nodeId, globalPos);
            }
            else
            {
                emit rowDragMoved(globalPos);
            }
        }
        return;
    }

    if(m_pendingPressRow >= 0 && (event->buttons() & Qt::LeftButton))
    {
        if((event->pos() - m_pressPos).manhattanLength() > 6)
        {
            m_dragRowIndex = m_pendingPressRow;
            m_pendingPressRow = -1;
            m_dragIsOutside = false;
            grabMouse();
            updateGhostPosition(event->pos(), false);
            update();
        }
    }
}

void MasterLayerStackWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if(m_dragRowIndex >= 0)
    {
        releaseMouse();
        QPoint globalPos = event->globalPosition().toPoint();

        if(m_dragIsOutside)
        {
            m_dragRowIndex = -1;
            m_dragIsOutside = false;
            clearGhost();
            emit rowDragEnded(globalPos);
            update();
            return;
        }

        QString nodeId = m_rows[m_dragRowIndex].nodeId;
        bool shouldCommit = m_ghostActive;
        int targetIndex = m_ghostInsertIndex;

        m_dragRowIndex = -1;
        clearGhost();

        if(shouldCommit)
            emit layerReordered(nodeId, targetIndex);

        update();
        return;
    }
    if(m_pendingPressRow >= 0)
    {
        emit layerActivated(m_rows[m_pendingPressRow].nodeId);
        m_pendingPressRow = -1;
    }
}