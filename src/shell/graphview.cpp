#include "graphview.hpp"

#include "project.hpp"
#include "node/node-base.hpp"
#include "node/node-export.hpp"

// ==== NODE ====
QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if(change == ItemPositionChange || change == ItemPositionHasChanged)
        updateConnectedEdges();
    if(change == ItemSelectedHasChanged && m_selectionOutline)
        m_selectionOutline->setVisible(value.toBool());
    return QGraphicsItemGroup::itemChange(change, value);
}
void NodeItem::updateConnectedEdges()
{
    for(EdgeItem* edge : m_connectedEdges)
        edge->updatePath();
}
void NodeItem::paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*)
{}
static QColor nodeTitleColor(NodeType type)
{
    switch(type)
    {
        case NodeType::Raster:       return QColor(66, 133, 165);  // blue
        case NodeType::GaussianBlur: return QColor(155, 89, 182);  // purple
        case NodeType::ChannelSplit: return QColor(230, 126, 34);  // orange
        case NodeType::Move:         return QColor(46, 160, 67);   // green
        case NodeType::Compositor:   return QColor(180, 60, 60);   // red
        case NodeType::Reference:    return QColor(120, 120, 120); // grey
        default:                     return QColor(90, 90, 90);
    }
}

// ==== EDGE ====
void EdgeItem::updatePath()
{
    if(!m_from || !m_to) return;

    QPointF fromCenter = m_from->outputPortScene();
    QPointF toCenter   = m_to->inputPortScene();

    QPainterPath path(fromCenter);
    float midX = (fromCenter.x() + toCenter.x()) / 2.0f;
    path.cubicTo(QPointF(midX, fromCenter.y()), QPointF(midX, toCenter.y()), toCenter);
    setPath(path);
}

// ==== GRAPH ====
// constructor
NodeGraphView::NodeGraphView(std::shared_ptr<Project> project, QWidget* parent)
    : QGraphicsView(parent), m_project(std::move(project))
{
    // setup
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    
    setDragMode(QGraphicsView::NoDrag);
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setBackgroundBrush(QColor(24, 24, 24));
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setAcceptDrops(true);

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    constexpr float kWorldExtent = 15000.0f;
    setSceneRect(-kWorldExtent, -kWorldExtent, kWorldExtent * 2, kWorldExtent * 2);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setFocusPolicy(Qt::StrongFocus); // ensure keyPressEvent (F key) reaches this widget

    // origin "+"
    constexpr float kMarkerSize = 14.0f;
    QPen markerPen(QColor(90, 90, 100));
    markerPen.setWidth(0);
    auto* hLine = m_scene->addLine(-kMarkerSize, 0, kMarkerSize, 0, markerPen);
    auto* vLine = m_scene->addLine(0, -kMarkerSize, 0, kMarkerSize, markerPen);
    hLine->setZValue(-1);
    vLine->setZValue(-1);

    //// connects
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());
    // mini map
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
    // mastercomp widget
    m_masterWidget = new MasterCompositorWidget(this);
    connect(m_masterWidget, &MasterCompositorWidget::geometryUpdated, this, [this]{
        constexpr int kMargin = 12;
        m_masterWidget->move(width() - m_masterWidget->width() - kMargin, kMargin);
    });
    connect(m_masterWidget, &MasterCompositorWidget::layerActivated, this, [this](QString id){
        auto it = m_nodeItems.find(id);
        if(it != m_nodeItems.end()) centerOn(it->second->pos() + it->second->m_localRect.center());
    });
    connect(m_masterWidget, &MasterCompositorWidget::rowDragStarted, this, [this](QString id, QPoint globalPos){
    m_connectDrag = { true, id, mapFromGlobal(globalPos) };
    viewport()->update();
    });
    connect(m_masterWidget, &MasterCompositorWidget::rowDragMoved, this, [this](QPoint globalPos){
        if(!m_connectDrag.active) return;
        m_connectDrag.currentScreenPos = mapFromGlobal(globalPos);
        viewport()->update();
    });
    connect(m_masterWidget, &MasterCompositorWidget::rowDragEnded, this, [this](QPoint globalPos){
        if(!m_connectDrag.active) return;
        QPoint panelLocal = m_masterWidget->mapFromGlobal(globalPos);
        if(!m_masterWidget->rect().contains(panelLocal))
            m_project->removeMasterLayer(m_connectDrag.sourceNodeId); // dropped outside panel = disconnect
        m_connectDrag = {};
        viewport()->update();
    });
    connect(m_masterWidget, &MasterCompositorWidget::layerReordered, this, [this](QString nodeId, int newRowIndex){
        size_t count = m_project->m_masterCompositor->m_layers.size();
        size_t newLayerIndex = count - 1 - (size_t)newRowIndex;
        m_project->moveMasterLayer(nodeId, newLayerIndex);
    });
    connect(m_masterWidget, &MasterCompositorWidget::rowLayoutChanged, this, [this]{
        viewport()->update();
    });

    // project
    connect(m_project.get(), &Project::signalNodePosChanged, this, [this](QString id, float x, float y){
        auto it = m_nodeItems.find(id);
        if(it != m_nodeItems.end() && it->second->pos() != QPointF(x, y))
            it->second->setPos(x, y);
    });
    connect(m_project.get(), &Project::signalNodeAdded, this, [this](QString id){
        createNodeItem(id);
    });
    connect(m_project.get(), &Project::signalNodeRemoved, this, [this](QString id){
        removeNodeItem(id);
    });
    connect(m_project.get(), &Project::signalEdgeAdded, this, [this](QString fromID, QString toID){
        createEdgeItem(fromID, toID);
    });
    connect(m_project.get(), &Project::signalEdgeRemoved, this, [this](QString fromID, QString toID){
        removeEdgeItem(fromID, toID);
    });
    connect(m_project.get(), &Project::signalMasterLayersChanged, this, &NodeGraphView::refreshMasterWidget);
    refreshMasterWidget();

    rebuildFromProject();

    // thumbnail timer
    Node::s_onNodeDirty = [this](Node* node){
        const QString& id = node->m_meta.id;
        if(m_dirtyThumbnailSet.insert(id).second)
            m_dirtyThumbnailQueue.push_back(id);
    };

    m_thumbnailTimer = new QTimer(this);
    connect(m_thumbnailTimer, &QTimer::timeout, this, [this]{ drainThumbnailUpdates(); });
    m_thumbnailTimer->start(100);
    m_thumbnailClock.start();
}

// node thumbnails
QImage NodeGraphView::renderNodeThumbnail(Node& node, int outW, int outH)
{
    BoundsI b = node.computeBounds();
    if(!b.valid) return QImage();

    int w = b.width();
    int h = b.height();

    constexpr int kMaxNativeDim = 256;
    float clampScale = std::min(1.0f, (float)kMaxNativeDim / std::max(w, h));
    int nativeW = std::max(1, (int)(w * clampScale));
    int nativeH = std::max(1, (int)(h * clampScale));

    QImage full(nativeW, nativeH, QImage::Format_RGBA8888);
    for(int y = 0; y < nativeH; y++)
    {
        int srcY = b.minY + (int)((float)y / nativeH * h);
        RGBA* dstRow = reinterpret_cast<RGBA*>(full.scanLine(y));
        for(int x = 0; x < nativeW; x++)
        {
            int srcX = b.minX + (int)((float)x / nativeW * w);
            dstRow[x] = node.sampleBlended(srcX, srcY);
        }
    }

    return full.scaled(outW, outH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
void NodeGraphView::refreshNodeThumbnail(const QString& id)
{
    auto nodeIt = m_project->m_nodes.find(id);
    auto itemIt = m_nodeItems.find(id);
    if(nodeIt == m_project->m_nodes.end() || itemIt == m_nodeItems.end()) return;

    Node& node = *nodeIt->second;
    NodeItem* item = itemIt->second;
    if(!item->m_thumbnailItem) return;

    constexpr int kSupersample = 3;
    constexpr float kMaxZoom = 5.0f;

    int displayW = (int)item->m_thumbnailRect.width();
    int displayH = (int)item->m_thumbnailRect.height();

    BoundsI b = node.computeBounds();
    int renderW = (int)(displayW * kMaxZoom * kSupersample);
    int renderH = (int)(displayH * kMaxZoom * kSupersample);
    if(b.valid && b.width() > 0 && b.height() > 0)
    {
        float srcAspect = (float)b.width() / (float)b.height();
        float boxAspect = (float)displayW / (float)displayH;
        if(srcAspect > boxAspect)
            renderH = (int)(renderW / srcAspect);
        else
            renderW = (int)(renderH * srcAspect);
    }
    renderW = std::max(1, renderW);
    renderH = std::max(1, renderH);

    QImage img = renderNodeThumbnail(node, renderW, renderH);

    if(img.isNull())
    {
        item->m_thumbnailItem->setPixmap(QPixmap());
        return;
    }

    int storedW = std::max(1, renderW / kSupersample);
    int storedH = std::max(1, renderH / kSupersample);
    QPixmap pix = QPixmap::fromImage(img).scaled(
        QSize(storedW, storedH), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    item->m_thumbnailItem->setTransformationMode(Qt::SmoothTransformation);
    item->m_thumbnailItem->setScale(1.0f / kMaxZoom);

    QPointF offset(
        (displayW - pix.width()  / kMaxZoom) / 2.0,
        (displayH - pix.height() / kMaxZoom) / 2.0);
    item->m_thumbnailItem->setPos(item->m_thumbnailRect.topLeft() + offset);
    item->m_thumbnailItem->setPixmap(pix);
}
void NodeGraphView::drainThumbnailUpdates()
{
    if(m_dirtyThumbnailQueue.empty()) return;

    QElapsedTimer budget;
    budget.start();
    constexpr qint64 kBudgetMs = 4;

    while(!m_dirtyThumbnailQueue.empty() && budget.elapsed() < kBudgetMs)
    {
        QString id = m_dirtyThumbnailQueue.front();
        m_dirtyThumbnailQueue.pop_front();
        m_dirtyThumbnailSet.erase(id);

        qint64 now = m_thumbnailClock.elapsed();
        qint64& last = m_lastThumbnailRefreshMs[id]; // default-inits to 0
        if(now - last < kThumbnailMinIntervalMs)
        {
            m_dirtyThumbnailSet.insert(id);
            m_dirtyThumbnailQueue.push_back(id);
            continue;
        }

        last = now;
        refreshNodeThumbnail(id);
    }
}

//// graph funcs
// nodes - utils
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

    constexpr float kMinFrameW = 400.0f;
    constexpr float kMinFrameH = 320.0f;

    if(bounds.width() < kMinFrameW)
    {
        float pad = (kMinFrameW - bounds.width()) / 2.0f;
        bounds.adjust(-pad, 0, pad, 0);
    }
    if(bounds.height() < kMinFrameH)
    {
        float pad = (kMinFrameH - bounds.height()) / 2.0f;
        bounds.adjust(0, -pad, 0, pad);
    }

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
void NodeGraphView::deleteSelectedNodes()
{
    QString masterId = m_project->m_masterCompositor->m_meta.id;

    std::vector<QString> ids;
    for(QGraphicsItem* item : m_scene->selectedItems())
    {
        auto* node = qgraphicsitem_cast<NodeItem*>(item);
        if(node && node->m_id != masterId)
            ids.push_back(node->m_id);
    }

    m_project->deleteNodes(ids);
}
void NodeGraphView::updateRubberBandSelection()
{
    QRectF sceneRect = mapToScene(m_rubberBand->geometry()).boundingRect();

    for(auto& [id, node] : m_nodeItems)
    {
        bool intersects  = sceneRect.intersects(QRectF(node->pos(), node->m_localRect.size()));
        bool wasSelected = m_rubberBandAdditive && m_rubberBandBaseSelected.contains(id);
        bool target      = intersects ? !wasSelected : wasSelected;

        if(node->isSelected() != target)
            node->setSelected(target);
    }
}

// signaled funcs - graph item related
void NodeGraphView::createNodeItem(const QString& id)
{
    auto it = m_project->m_nodes.find(id);
    if(it == m_project->m_nodes.end()) return;
    auto& node = it->second;

    QFont titleFont = QGraphicsTextItem().font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 0.5);
    QFontMetricsF titleFm(titleFont);
    float kTitlePadding = 8.0f;
    float kTitleH = titleFm.height() + kTitlePadding;

    float kBoxW   = 140.0f;
    float kThumbH = 100.0f;
    float kBoxH   = kTitleH + kThumbH;
    float kPortRadius = 5.0f;

    bool isMaster = (id == m_project->m_masterCompositor->m_meta.id);
    bool isSource = (node->m_meta.type == NodeType::Raster);

    auto* nodeItem = new NodeItem();
    nodeItem->m_id = id;
    nodeItem->m_localRect = QRectF(0, 0, kBoxW, kBoxH);
    nodeItem->m_thumbnailRect = QRectF(4, kTitleH + 4, kBoxW - 8, kThumbH - 8);

    if(!isMaster)
    {
        auto makePort = [&](QPointF localCenter, QColor color) {
            auto* dot = new QGraphicsEllipseItem(
                localCenter.x() - kPortRadius, localCenter.y() - kPortRadius,
                kPortRadius * 2, kPortRadius * 2);
            dot->setBrush(QBrush(color));
            dot->setPen(QPen(QColor(20, 20, 20), 1));
            dot->setZValue(2);
            nodeItem->addToGroup(dot);
            return dot;
        };
        nodeItem->m_outputPort = makePort(nodeItem->outputPortLocal(), QColor(120, 170, 220));
        if(!isSource)
            nodeItem->m_inputPort = makePort(nodeItem->inputPortLocal(), QColor(160, 160, 160));
    }

    // title bar
    auto* titleRect = new QGraphicsRectItem(0, 0, kBoxW, kTitleH);
    titleRect->setPen(QPen(QColor(20, 20, 20)));
    titleRect->setBrush(QBrush(nodeTitleColor(node->m_meta.type)));

    auto* titleText = new QGraphicsTextItem(node->m_meta.label);
    titleText->setDefaultTextColor(Qt::white);
    QFont f = titleText->font();
    f.setStyleStrategy(QFont::PreferAntialias); 
    f.setPointSizeF(f.pointSizeF() * 0.5);
    f.setKerning(true);
    titleText->setFont(f);
    titleText->setPos(6, (kTitleH - titleText->boundingRect().height()) / 2.0f);

    // body / thumbnail background
    auto* bodyRect = new QGraphicsRectItem(0, kTitleH, kBoxW, kThumbH);
    bodyRect->setPen(QPen(QColor(60, 60, 60), 0));
    bodyRect->setBrush(QBrush(QColor(32, 32, 32)));

    nodeItem->addToGroup(titleRect);
    nodeItem->addToGroup(titleText);
    nodeItem->addToGroup(bodyRect);

    auto* pixItem = new QGraphicsPixmapItem();
    pixItem->setPos(nodeItem->m_thumbnailRect.topLeft());
    pixItem->setTransformationMode(Qt::SmoothTransformation);
    QPainterPath clipPath;
    clipPath.addRect(QRectF(QPointF(0,0), nodeItem->m_thumbnailRect.size()));

    nodeItem->addToGroup(pixItem);
    nodeItem->m_thumbnailItem = pixItem;

    auto* selectionOutline = new QGraphicsRectItem(nodeItem->m_localRect);
    QPen selPen(QColor(120, 170, 220), 2);
    selPen.setCosmetic(true);
    selectionOutline->setPen(selPen);
    selectionOutline->setBrush(Qt::NoBrush);
    selectionOutline->setZValue(5);
    selectionOutline->setVisible(false);
    nodeItem->addToGroup(selectionOutline);
    nodeItem->m_selectionOutline = selectionOutline;

    nodeItem->setPos(node->m_meta.x, node->m_meta.y);
    nodeItem->setFlag(QGraphicsItem::ItemIsMovable, true);
    nodeItem->setFlag(QGraphicsItem::ItemIsSelectable, true);
    nodeItem->setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    nodeItem->setZValue(1);

    m_scene->addItem(nodeItem);
    m_nodeItems[id] = nodeItem;

    refreshNodeThumbnail(id);
}
void NodeGraphView::removeNodeItem(const QString& id)
{
    auto it = m_nodeItems.find(id);
    if(it == m_nodeItems.end()) return;

    NodeItem* node = it->second;

    m_scene->removeItem(node);
    delete node;
    m_nodeItems.erase(it);

    // updateMiniMap();
}
void NodeGraphView::createEdgeItem(const QString& fromID, const QString& toID)
{
    auto fromIt = m_nodeItems.find(fromID);
    auto toIt   = m_nodeItems.find(toID);
    if(fromIt == m_nodeItems.end() || toIt == m_nodeItems.end()) return;

    QString key = edgeKey(fromID, toID);
    if(m_edgeItems.contains(key)) return;

    auto* edge = new EdgeItem();
    edge->m_from = fromIt->second;
    edge->m_to = toIt->second;
    edge->setPen(QPen(QColor(120, 170, 220), 2));
    edge->setZValue(0);

    fromIt->second->m_connectedEdges.push_back(edge);
    toIt->second->m_connectedEdges.push_back(edge);

    m_scene->addItem(edge);
    edge->updatePath();
    m_edgeItems.emplace(key, edge);

    // updateMiniMap();
}
void NodeGraphView::removeEdgeItem(const QString& fromID, const QString& toID)
{
    auto it = m_edgeItems.find(edgeKey(fromID, toID));
    if(it == m_edgeItems.end()) return;

    EdgeItem* edge = it->second;
    if(edge->m_from) std::erase(edge->m_from->m_connectedEdges, edge);
    if(edge->m_to) std::erase(edge->m_to->m_connectedEdges, edge);

    m_scene->removeItem(edge);
    m_edgeItems.erase(it);
    delete edge;

    // updateMiniMap();
}

void NodeGraphView::rebuildFromProject()
{
    if(!m_nodeItems.empty() || !m_edgeItems.empty())
        nukeGraph();

    QString masterId = m_project->m_masterCompositor->m_meta.id;

    for(auto& [id, node] : m_project->m_nodes)
    {
        if(id == masterId) continue;
        if(m_nodeItems.contains(id)) continue;
        createNodeItem(id);
    }

    for(auto& [id, node] : m_project->m_nodes)
    {
        if(id == masterId) continue;
        for(auto& input : node->getInputs())
        {
            if(!input) continue;
            createEdgeItem(input->m_meta.id, id);
        }
    }

    updateMiniMap();
}
void NodeGraphView::nukeGraph()
{
    for(auto& [key, edge] : m_edgeItems)
    {
        if(edge->m_from) std::erase(edge->m_from->m_connectedEdges, edge);
        if(edge->m_to)   std::erase(edge->m_to->m_connectedEdges, edge);
        m_scene->removeItem(edge);
        delete edge;
    }
    m_edgeItems.clear();

    for(auto& [id, node] : m_nodeItems)
    {
        m_scene->removeItem(node);
        delete node;
    }
    m_nodeItems.clear();
}

//// widgets
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
        for(auto& [key, edge] : m_edgeItems)
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
    void NodeGraphView::refreshMasterWidget()
    {
        auto& layers = m_project->m_masterCompositor->m_layers;
        std::vector<MasterCompositorWidget::Row> rows;
        rows.reserve(layers.size());
        for(auto it = layers.rbegin(); it != layers.rend(); ++it)
            rows.push_back({ (*it)->m_meta.id, (*it)->m_meta.label });
        m_masterWidget->setRows(rows);
    }
//

//// events
    // draw
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

            size_t rowIndex = m_project->m_masterCompositor->m_layers.size() - 1 - i;
            QPoint panelGlobal = m_masterWidget->mapToGlobal(m_masterWidget->portPosFor((int)rowIndex).toPoint());
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
                drawDragPreview(p, mapFromScene(it->second->outputPortScene()), m_connectDrag.currentScreenPos);

            if(!m_connectDrag.hoverInputNodeId.isEmpty())
            {
                auto targetIt = m_nodeItems.find(m_connectDrag.hoverInputNodeId);
                if(targetIt != m_nodeItems.end())
                {
                    QPointF ring = mapFromScene(targetIt->second->inputPortScene());
                    p.setPen(QPen(QColor(120, 220, 150), 2));
                    p.setBrush(Qt::NoBrush);
                    p.drawEllipse(ring, 9, 9);
                }
            }
        }
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
    QPainterPath NodeGraphView::drawConnectorPath(QPainter& p, QPointF origin, QPointF target)
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
    void NodeGraphView::drawDragPreview(QPainter& p, QPointF origin, QPointF cursor)
    {
        QLinearGradient grad(origin, cursor);
        grad.setColorAt(0.0, QColor(120, 170, 220, 200));
        grad.setColorAt(1.0, QColor(120, 170, 220, 80));
        QPen pen(QBrush(grad), 2, Qt::DashLine);
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
        p.drawLine(origin, cursor);
    }

    // mouse
    void NodeGraphView::mousePressEvent(QMouseEvent* event)
    {
        if(event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && m_spaceHeld))
        {
            m_panning = true;
            m_lastPanPos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            return;
        }

        if(event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier))
        {
            m_ctrlClickPending = true;
            m_rubberBandOrigin = event->pos();
            return;
        }

        QString masterId = m_project->m_masterCompositor->m_meta.id;
        for(auto& [id, node] : m_nodeItems)
        {
            if(id == masterId || !node->m_outputPort) continue;
            QPointF portScreen = mapFromScene(node->outputPortScene());
            if(QLineF(portScreen, event->pos()).length() < 10.0)
            {
                m_connectDrag = { true, id, event->pos(), QString() };
                setDragMode(QGraphicsView::NoDrag);
                return;
            }
        }

        // clear selection / start plain rubber-band drag
        QGraphicsItem* hit = itemAt(event->pos());
        NodeItem* hitNode = qgraphicsitem_cast<NodeItem*>(hit);
        if(!hitNode && hit)
            hitNode = qgraphicsitem_cast<NodeItem*>(hit->group());

        if(event->button() == Qt::LeftButton && !hitNode)
        {
            m_plainClickPending = true;
            m_rubberBandOrigin = event->pos();
            return;
        }

        QGraphicsView::mousePressEvent(event);
    }
    void NodeGraphView::mouseReleaseEvent(QMouseEvent* event)
    {
        if(m_panning)
        {
            m_panning = false;
            unsetCursor();
            return;
        }

        if(m_rubberBandSelecting)
        {
            updateRubberBandSelection();
            m_rubberBand->hide();
            m_rubberBandSelecting = false;
            m_rubberBandBaseSelected.clear();
            return;
        }

        if(m_ctrlClickPending)
        {
            m_ctrlClickPending = false;

            QGraphicsItem* hit = itemAt(event->pos());
            NodeItem* node = qgraphicsitem_cast<NodeItem*>(hit);
            if(!node && hit)
                node = qgraphicsitem_cast<NodeItem*>(hit->group());

            if(node) node->setSelected(!node->isSelected());
            return;
        }

        if(m_plainClickPending)
        {
            m_plainClickPending = false;
            m_scene->clearSelection();
            return;
        }

        if(m_connectDrag.active)
        {
            QString sourceId = m_connectDrag.sourceNodeId;
            QString targetId = m_connectDrag.hoverInputNodeId;

            // regular graph node
            if(!targetId.isEmpty())
            {
                m_project->connectNodes(sourceId, targetId);
                m_connectDrag = {};
                setDragMode(QGraphicsView::NoDrag);
                viewport()->update();
                return;
            }

            // master widget
            QPoint panelLocal = m_masterWidget->mapFromGlobal(mapToGlobal(event->pos()));
            bool droppedOnPanel = m_masterWidget->rect().contains(panelLocal);
            size_t insertIndex = (size_t)m_masterWidget->ghostInsertIndex();

            m_masterWidget->clearGhost();
            m_connectDrag = {};
            setDragMode(QGraphicsView::NoDrag);

            if(droppedOnPanel)
            {
                size_t count = m_project->m_masterCompositor->m_layers.size();
                size_t layerIndex = count - (size_t)insertIndex;
                m_project->addNodeToMasterAt(sourceId, layerIndex);
            }

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
        if(m_panning)
        {
            QPoint delta = event->pos() - m_lastPanPos;
            m_lastPanPos = event->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            updateMiniMap();
            return;
        }

        if(m_ctrlClickPending || m_plainClickPending)
        {
            constexpr int kDragThreshold = 4;
            if((event->pos() - m_rubberBandOrigin).manhattanLength() > kDragThreshold)
            {
                m_rubberBandAdditive = m_ctrlClickPending;
                m_ctrlClickPending = false;
                m_plainClickPending = false;
                m_rubberBandSelecting = true;

                m_rubberBandBaseSelected.clear();
                if(m_rubberBandAdditive)
                    for(auto& [id, node] : m_nodeItems)
                        if(node->isSelected())
                            m_rubberBandBaseSelected.insert(id);

                m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, QSize()));
                m_rubberBand->show();
                updateRubberBandSelection();
            }
            else
            {
                return;
            }
        }

        if(m_rubberBandSelecting)
        {
            m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, event->pos()).normalized());
            updateRubberBandSelection();
            return;
        }

        if(m_connectDrag.active)
        {
            m_connectDrag.currentScreenPos = event->pos();

            QPoint panelLocal = m_masterWidget->mapFromGlobal(mapToGlobal(event->pos()));
            if(m_masterWidget->rect().contains(panelLocal))
                m_masterWidget->updateGhostPosition(panelLocal, true);
            else
                m_masterWidget->clearGhost();

            m_connectDrag.hoverInputNodeId.clear();
            for(auto& [id, node] : m_nodeItems)
            {
                if(id == m_connectDrag.sourceNodeId || !node->m_inputPort) continue;
                if(QLineF(mapFromScene(node->inputPortScene()), event->pos()).length() < 10.0)
                {
                    m_connectDrag.hoverInputNodeId = id;
                    break;
                }
            }

            viewport()->update();
            return;
        }

        QGraphicsView::mouseMoveEvent(event);

        if(event->buttons() & Qt::LeftButton)
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

    // drag+drop
    void NodeGraphView::dragEnterEvent(QDragEnterEvent* event)
    {
        if(event->mimeData()->hasUrls())
            event->acceptProposedAction();
    }
    void NodeGraphView::dragMoveEvent(QDragMoveEvent* event)
    {
        if(event->mimeData()->hasUrls())
            event->acceptProposedAction();
    }
    void NodeGraphView::dropEvent(QDropEvent* event)
    {
        if(!event->mimeData()->hasUrls()) return;

        static const QStringList kImageExts = { "png", "jpg", "jpeg", "bmp", "tga", "gif" };

        QPointF scenePos = mapToScene(event->position().toPoint());

        for(const QUrl& url : event->mimeData()->urls())
        {
            if(!url.isLocalFile()) continue;

            QString path = url.toLocalFile();
            if(!kImageExts.contains(QFileInfo(path).suffix().toLower())) continue;

            m_project->createRasterNodeFromImage(path, (float)scenePos.x(), (float)scenePos.y());
            scenePos += QPointF(30, 30); // stagger multi-file drops so they don't stack exactly
        }

        event->acceptProposedAction();
    }

    // keyboard
    void NodeGraphView::keyPressEvent(QKeyEvent* event)
    {
        if(event->key() == Qt::Key_Space && !event->isAutoRepeat())
        {
            m_spaceHeld = true;
            return;
        }
        if(event->key() == Qt::Key_F) // frame nodes
        {
            frameAllNodes();
            return;
        }
        if(event->key() == Qt::Key_D && (event->modifiers() & Qt::ControlModifier)) // dup node
        {
            duplicateSelectedNode();
            return;
        }
        if(event->key() == Qt::Key_M) // new raster node
        {
            m_project->createRasterNode(0.0f, 0.0f);
            return;
        }
        if(event->key() == Qt::Key_Delete)
        {
            deleteSelectedNodes();
            return;
        }
        if(event->key() == Qt::Key_E)
        {
            bool ok = exportMasterToPath(*m_project->m_masterCompositor, "output.png");
            qDebug() << "export" << (ok ? "OK" : "FAILED") << "cwd:" << QDir::currentPath();
            return;
        }
        QGraphicsView::keyPressEvent(event);
    }
    void NodeGraphView::keyReleaseEvent(QKeyEvent* event)
    {
        if(event->key() == Qt::Key_Space && !event->isAutoRepeat())
            m_spaceHeld = false;
        QGraphicsView::keyReleaseEvent(event);
    }
//