#include "graphview.hpp"
#include "project.hpp"

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
NodeGraphView::NodeGraphView(std::shared_ptr<Project> project, QWidget* parent)
    : QGraphicsView(parent), m_project(std::move(project))
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setBackgroundBrush(QColor(24, 24, 24));
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

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

    m_layerStack = new MasterLayerStackWidget(this);
    connect(m_layerStack, &MasterLayerStackWidget::geometryUpdated, this, [this]{
        constexpr int kMargin = 12;
        m_layerStack->move(width() - m_layerStack->width() - kMargin, kMargin);
    });

    connect(m_project.get(), &Project::nodeGraphChanged, this, [this]{
        std::vector<MasterLayerStackWidget::Row> rows;
        for(auto& layer : m_project->m_masterCompositor->m_layers)
            rows.push_back({ layer->m_meta.id, layer->m_meta.label });
        m_layerStack->setRows(rows);
    });
    connect(m_layerStack, &MasterLayerStackWidget::layerActivated, this, [this](QString id){
        auto it = m_nodeItems.find(id);
        if(it != m_nodeItems.end()) centerOn(it->second->pos() + it->second->m_localRect.center());
    });
    connect(m_layerStack, &MasterLayerStackWidget::rowDragStarted, this, [this](QString id, QPoint globalPos){
    m_connectDrag = { true, id, mapFromGlobal(globalPos) };
    viewport()->update();
    });
    connect(m_layerStack, &MasterLayerStackWidget::rowDragMoved, this, [this](QPoint globalPos){
        if(!m_connectDrag.active) return;
        m_connectDrag.currentScreenPos = mapFromGlobal(globalPos);
        viewport()->update();
    });
    connect(m_layerStack, &MasterLayerStackWidget::rowDragEnded, this, [this](QPoint globalPos){
        if(!m_connectDrag.active) return;
        QPoint panelLocal = m_layerStack->mapFromGlobal(globalPos);
        if(!m_layerStack->rect().contains(panelLocal))
            m_project->removeMasterLayerByNodeId(m_connectDrag.sourceNodeId); // dropped outside panel = disconnect
        m_connectDrag = {};
        viewport()->update();
    });
    connect(m_layerStack, &MasterLayerStackWidget::layerReordered, this, [this](QString nodeId, int newIndex){
        m_project->moveMasterLayer(nodeId, newIndex); // purely a stack reorder - never touches connection state
    });

    connect(m_project.get(), &Project::nodeGraphChanged, this, &NodeGraphView::refreshFromProject);
    refreshFromProject();
    refreshLayerStackRows();

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
    hLine->setZValue(-1);
    vLine->setZValue(-1);
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
void NodeGraphView::duplicateSelectedNode()
{
    NodeItem* selected = nullptr;
    for(QGraphicsItem* item : m_scene->selectedItems())
        if(auto* n = qgraphicsitem_cast<NodeItem*>(item)) { selected = n; break; }
    if(!selected) return;

    if(selected->m_id == m_project->m_masterCompositor->m_meta.id) return;

    QPointF newPos = selected->pos() + QPointF(20, 20);
    QString newId = m_project->duplicateNode(selected->m_id, (float)newPos.x(), (float)newPos.y());
    if(newId.isEmpty()) return;

    auto it = m_nodeItems.find(newId);
    if(it != m_nodeItems.end())
    {
        m_scene->clearSelection();
        it->second->setSelected(true);
    }
}

// snapshot (source: project)
void NodeGraphView::refreshFromProject()
{ setSnapshot(m_project->buildGraphSnapshot()); }
void NodeGraphView::setSnapshot(const GraphSnapshot& snapshot)
{
    QPointF viewCenterScene = mapToScene(viewport()->rect().center());

    for(EdgeItem* edge : m_edgeItems)
    {
        m_scene->removeItem(edge);
        delete edge;
    }
    m_edgeItems.clear();

    for(auto& [id, node] : m_nodeItems)
        node->m_connectedEdges.clear();

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
        node->m_id = n.id;
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
    updateMiniMap();
}

// minimap
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

// master widget
void NodeGraphView::refreshLayerStackRows()
{
    std::vector<MasterLayerStackWidget::Row> rows;
    for(auto& layer : m_project->m_masterCompositor->m_layers)
        rows.push_back({ layer->m_meta.id, layer->m_meta.label });
    m_layerStack->setRows(rows);
}

//// events
void NodeGraphView::paintEvent(QPaintEvent* event) 
{
    QGraphicsView::paintEvent(event);

    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);

    m_connectorPaths.clear();

    for(size_t i = 0; i < m_project->m_masterCompositor->m_layers.size(); i++)
    {
        auto& layer = m_project->m_masterCompositor->m_layers[i];
        auto it = m_nodeItems.find(layer->m_meta.id);
        if(it == m_nodeItems.end()) continue;

        QPointF fromScene = it->second->pos() + QPointF(it->second->m_localRect.right(),
                                                        it->second->m_localRect.center().y());
        QPointF nodePort = mapFromScene(fromScene);

        // real target: this row's actual port, converted into viewport-local coordinates
        QPoint panelGlobal = m_layerStack->mapToGlobal(m_layerStack->portPosFor((int)i).toPoint());
        QPointF panelPort = viewport()->mapFromGlobal(panelGlobal);

        QPainterPath connector = drawConnectorPath(p, nodePort, panelPort);

        p.setPen(QPen(QColor(120,170,220)));
        p.setBrush(QColor(120,170,220));
        p.drawRect(QRectF(nodePort.x()-3, nodePort.y()-3, 6, 6));

        m_connectorPaths.emplace_back(layer->m_meta.id, connector);
    }

    if(m_connectDrag.active)
    {
        auto it = m_nodeItems.find(m_connectDrag.sourceNodeId);
        if(it != m_nodeItems.end())
        {
            QPointF fromScene = it->second->pos() + QPointF(it->second->m_localRect.right(),
                                                            it->second->m_localRect.center().y());
            drawDragPreview(p, mapFromScene(fromScene), m_connectDrag.currentScreenPos);
        }
    }
}
// mouse
void NodeGraphView::mousePressEvent(QMouseEvent* event) 
{
    for(auto& [id, node] : m_nodeItems)
    {
        QPointF portScene = node->pos() + QPointF(node->m_localRect.right(), node->m_localRect.center().y());
        QPointF portScreen = mapFromScene(portScene);
        if(QLineF(portScreen, event->pos()).length() < 10.0)
        {
            m_connectDrag = { true, id, event->pos() };
            setDragMode(QGraphicsView::NoDrag);
            return;
        }
    }
    QGraphicsView::mousePressEvent(event);
}
void NodeGraphView::mouseReleaseEvent(QMouseEvent* event) 
{
    if(m_connectDrag.active)
    {
        QPoint panelLocal = m_layerStack->mapFromGlobal(mapToGlobal(event->pos()));
        bool droppedOnPanel = m_layerStack->rect().contains(panelLocal);
        size_t insertIndex = (size_t)m_layerStack->ghostInsertIndex();
        QString sourceId = m_connectDrag.sourceNodeId;

        m_layerStack->clearGhost();
        m_connectDrag = {};
        setDragMode(QGraphicsView::ScrollHandDrag);

        if(droppedOnPanel)
            m_project->addNodeToMasterAt(sourceId, insertIndex);

        viewport()->update();
        return;
    }

    NodeItem* grabbed = qgraphicsitem_cast<NodeItem*>(scene()->mouseGrabberItem());
    QGraphicsView::mouseReleaseEvent(event);

    if(grabbed)
    {
        m_project->setNodePosition(grabbed->m_id, (float)grabbed->pos().x(), (float)grabbed->pos().y());
        updateMiniMap();
    }
}
void NodeGraphView::mouseMoveEvent(QMouseEvent* event) 
{
    if(m_connectDrag.active)
    {
        m_connectDrag.currentScreenPos = event->pos();
        QPoint panelLocal = m_layerStack->mapFromGlobal(mapToGlobal(event->pos()));
        if(m_layerStack->rect().contains(panelLocal))
            m_layerStack->updateGhostPosition(panelLocal, true);
        else
            m_layerStack->clearGhost();
        viewport()->update();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);

    if(event->buttons() & Qt::LeftButton)
        updateMiniMap();
}