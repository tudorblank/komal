#pragma once
#include <QWidget>
#include <QRectF>
#include <QString>
#include <QPainterPath>
#include <vector>

class MasterLayerStackWidget : public QWidget{
    Q_OBJECT
public:
    explicit MasterLayerStackWidget(QWidget* parent = nullptr);

    struct Row { QString nodeId; QString label; };
    void setRows(std::vector<Row> rows);

    QPointF portPosFor(int rowIndex) const;
    QRectF rowGeometry(int index) const;

    void updateGhostPosition(QPoint widgetLocalPos, bool isNewLayer);
    void clearGhost();
    int ghostInsertIndex() const { return m_ghostInsertIndex; }

signals:
    void geometryUpdated();
    void layerActivated(QString nodeId);

    void rowDragStarted(QString nodeId, QPoint globalPos);
    void rowDragMoved(QPoint globalPos);
    void rowDragEnded(QPoint globalPos);

    void layerReordered(QString nodeId, int newIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::vector<Row> m_rows;
    int m_ghostInsertIndex = -1;
    bool m_ghostActive = false;

    int m_pendingPressRow = -1;
    QPoint m_pressPos;

    int m_dragRowIndex = -1;
    bool m_dragIsOutside = false;

    static constexpr int kRowH = 44;
    static constexpr int kRowBoxWidth = 140;
    static constexpr int kStubMargin = 60;
    static constexpr int kWidth = kStubMargin + kRowBoxWidth + 4;
    static constexpr qreal kPortGrabTolerance = 10.0;
    static constexpr qreal kStubLength = 50.0;
    static constexpr qreal kStubArch = 14.0;

    void recomputeSize();
    static bool pointNearSegment(QPointF p, QPointF a, QPointF b, qreal tol);
    static QPointF stubEndPoint(QPointF portPt);
    static QPainterPath stubPath(QPointF portPt);
};