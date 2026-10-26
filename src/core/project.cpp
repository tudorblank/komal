#include "project.hpp"
#include "core/perf.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace {

void paintStarterContent(RasterRootNode& background, RasterRootNode& paint)
{
    background.edit([](RasterEditor& ed) {
        for (int y = 0; y < 192; ++y)   // gradient
            for (int x = 0; x < 256; ++x)
                ed.setPixel(x, y, { uint8_t(x), uint8_t(y * 255 / 191), 160, 255 });
        for (int y = 0; y < 128; ++y)   // checkerboard, half-transparent squares
            for (int x = 0; x < 128; ++x) {
                const bool on = ((x / 8) + (y / 8)) % 2 == 0;
                ed.setPixel(300 + x, y, on ? RGBA{ 230, 230, 230, 255 } : RGBA{ 20, 20, 20, 128 });
            }
    });
    paint.edit([](RasterEditor& ed) {
        for (int y = -96; y <= 0; ++y)   // disc across negative tile coordinates
            for (int x = -96; x <= 0; ++x) {
                const int dx = x + 48, dy = y + 48;
                if (dx * dx + dy * dy <= 40 * 40) ed.setPixel(x, y, { 255, 160, 40, 255 });
            }
    });
}

}

void Project::init()
{
    m_master = CompositorNode::create();
    m_master->m_meta.label = "Master";
    m_master->enableCache(true);   // the viewport reads its output tile by tile
    adopt(m_master, 320.0f, 120.0f);

    const NodeId bg    = createRaster(40.0f, 0.0f);
    const NodeId paint = createRaster(40.0f, 370.0f);
    paintStarterContent(*rasterNode(m_rasters[0].id()), *rasterNode(m_rasters[1].id()));   // not undoable
    connect(bg,    m_master->m_meta.id, InputSlot::newLayer(0));
    connect(paint, m_master->m_meta.id, InputSlot::newLayer(1));
    setActiveRaster(paint);
}

// ---------------------------------------------------------------- read

Node* Project::find(NodeId id) const
{
    auto it = m_nodes.find(id);
    return it == m_nodes.end() ? nullptr : it->second.get();
}

NodePtr Project::findPtr(NodeId id) const
{
    auto it = m_nodes.find(id);
    return it == m_nodes.end() ? nullptr : it->second;
}

RasterRootNode* Project::activeRaster() const
{
    return static_cast<RasterRootNode*>(find(m_active));   // m_active only ever names a raster root
}

RasterRootNode* Project::rasterNode(RasterId id) const
{
    auto it = m_rasterNodes.find(id);
    return it == m_rasterNodes.end() ? nullptr : static_cast<RasterRootNode*>(find(it->second));
}

NodePtr Project::inputAt(NodeId to, const InputSlot& slot) const
{
    Node* n = find(to);
    if (!n) return nullptr;
    switch (slot.kind) {
    case InputSlot::Kind::Input:
        if (auto* u = dynamic_cast<UnaryNode*>(n)) return u->input();
        return nullptr;
    case InputSlot::Kind::Layer:
        if (auto* c = dynamic_cast<CompositorNode*>(n); c && slot.layer < c->layers().size()) return c->layers()[slot.layer];
        return nullptr;
    case InputSlot::Kind::NewLayer:
        return nullptr;   // a position, not an input
    case InputSlot::Kind::Mask:
        return n->signal(slot.signal).mask[int(slot.channel)];
    }
    return nullptr;
}

// ---------------------------------------------------------------- graph edits

NodeId Project::adopt(NodePtr n, float x, float y)
{
    const NodeId id = m_nextNodeId++;
    n->m_meta.id = id;
    n->m_meta.x  = x;
    n->m_meta.y  = y;
    m_nodes.emplace(id, n);
    m_order.push_back(std::move(n));
    emit(ProjectEventType::NodeAdded, id);
    return id;
}

NodeId Project::createRaster(float x, float y)
{
    const RasterId rid = RasterId(m_rasters.size());
    RasterData& data = m_rasters.emplace_back(rid);
    auto node = RasterRootNode::create(data);
    node->m_meta.label = "Raster " + std::to_string(++m_rasterLabel);
    const NodeId id = adopt(node, x, y);
    m_rasterNodes.emplace(rid, id);
    if (m_active == kNoNodeId) setActiveRaster(id);
    return id;
}

NodeId Project::importImage(const ImageFile& image, const ImportOptions& options)
{
    TileGrid tiles;
    tilesFromImage(tiles, { image.pixels, image.width, image.height }, 0, 0);
    return importTiles(tiles, image.name, options);
}

NodeId Project::importTiles(const TileGrid& tiles, const std::string& name, const ImportOptions& options)
{
    float x = options.nodeX, y = options.nodeY;
    if (!options.placeNode) {   // below the lowest raster in the graph
        x = 40.0f;
        y = 0.0f;
        for (const NodePtr& n : m_order)
            if (n->m_meta.type == NodeType::Raster) y = std::max(y, n->m_meta.y + 370.0f);
    }
    const NodeId prevActive = m_active;
    const NodeId id = createRaster(x, y);
    RasterRootNode& r = *static_cast<RasterRootNode*>(find(id));
    if (!name.empty()) r.m_meta.label = name;
    r.edit([&](RasterEditor& ed) { ed.applyTileGrid(tiles, { 255, CompositeMode::Replace }); });
    if (options.addToMaster) connect(id, m_master->m_meta.id, InputSlot::newLayer(uint32_t(m_master->layers().size())));
    if (options.makeActive) setActiveRaster(id);
    else if (m_active == id && prevActive != kNoNodeId) setActiveRaster(prevActive);   // createRaster takes it when there was none
    return id;
}

NodeId Project::createReference(float x, float y, NodeId target)
{
    NodePtr t = findPtr(target);
    if (t && isMaster(target)) t = nullptr;   // the master output is a sink
    return adopt(ReferenceNode::create(std::move(t)), x, y);
}

NodeId Project::createMove(float x, float y)       { return adopt(MoveNode::create(), x, y); }
NodeId Project::createCompositor(float x, float y) { return adopt(CompositorNode::create(), x, y); }

NodeId Project::createNode(NodeType type, float x, float y)
{
    switch (type) {
    case NodeType::Raster:     return createRaster(x, y);
    case NodeType::Reference:  return createReference(x, y);
    case NodeType::Move:       return createMove(x, y);
    case NodeType::Compositor: return createCompositor(x, y);
    }
    return kNoNodeId;
}

namespace {

// The one input a node passes through, if it has exactly one.
NodePtr singleInput(const Node& n)
{
    if (auto* u = dynamic_cast<const UnaryNode*>(&n)) return u->input();
    if (auto* c = dynamic_cast<const CompositorNode*>(&n); c && c->layers().size() == 1) return c->layers()[0];
    return nullptr;
}

}

NodePtr Project::bridgeSource(const Node& n, const absl::flat_hash_set<NodeId>& deleted) const
{
    NodePtr src = singleInput(n);
    while (src && deleted.contains(src->m_meta.id)) src = singleInput(*src);   // the graph is acyclic
    return src;
}

bool Project::deleteNodes(const std::vector<NodeId>& ids)
{
    absl::flat_hash_set<NodeId> doomed;
    for (NodeId id : ids)
        if (!isMaster(id) && find(id)) doomed.insert(id);
    if (doomed.empty()) return false;
    if (doomed.contains(m_preview.node)) cancelPreview();
    if (doomed.contains(m_strokeNode)) endStroke();   // committed while its node is still there

    // Re-feed every surviving slot a doomed node fed, before anything is removed (indices stay valid).
    absl::flat_hash_set<NodeId> rewired;
    for (const NodePtr& n : m_order) {
        const NodeId id = n->m_meta.id;
        if (doomed.contains(id)) continue;
        const auto source = [&](const NodePtr& old) -> NodePtr {
            if (!old || !doomed.contains(old->m_meta.id)) return nullptr;
            rewired.insert(id);
            return bridgeSource(*old, doomed);
        };
        if (auto* u = dynamic_cast<UnaryNode*>(n.get()))
            if (NodePtr src = source(u->input())) u->setInput(src);
        if (auto* c = dynamic_cast<CompositorNode*>(n.get()))
            for (size_t i = 0; i < c->layers().size(); ++i) {
                NodePtr old = c->layers()[i];   // copy: the replacement edits the stack
                if (NodePtr src = source(old); src && !c->has(src)) c->replaceInputInstance(old, src);
            }
        for (int s = 0; s < kSignalCount; ++s)
            for (int ch = 0; ch < kChannelCount; ++ch)
                if (NodePtr src = source(n->signal(NodeSignal(s)).mask[ch])) n->linkMask(src, NodeSignal(s), NodeChannel(ch));
    }

    // Whatever wasn't re-fed is dropped here.
    for (NodeId id : doomed) {
        auto it = m_nodes.find(id);
        NodePtr node = it->second;
        node->removeFromGraph();   // users lose it (and are told), it drops its inputs and masks
        m_nodes.erase(it);
        std::erase(m_order, node);
        if (node->m_meta.type == NodeType::Raster)
            absl::erase_if(m_rasterNodes, [&](const auto& kv) { return kv.second == id; });
        // The raster's pixels stay in m_rasters: history may still bring them back.
        emit(ProjectEventType::NodeRemoved, id);
    }
    for (NodeId id : rewired) emit(ProjectEventType::GraphChanged, id);
    emit(ProjectEventType::GraphChanged);

    if (doomed.contains(m_active)) {   // fall back to the most recently created raster
        NodeId next = kNoNodeId;
        for (auto r = m_order.rbegin(); r != m_order.rend(); ++r)
            if ((*r)->m_meta.type == NodeType::Raster) { next = (*r)->m_meta.id; break; }
        m_active = kNoNodeId;
        setActiveRaster(next);
        if (next == kNoNodeId) emit(ProjectEventType::ActiveRasterChanged);
    }
    return true;
}

void Project::setNodePosition(NodeId id, float x, float y)
{
    Node* n = find(id);
    if (!n || (n->m_meta.x == x && n->m_meta.y == y)) return;
    n->m_meta.x = x;
    n->m_meta.y = y;
    emit(ProjectEventType::NodeMoved, id);
}

void Project::setNodeLabel(NodeId id, std::string label)
{
    Node* n = find(id);
    if (!n || label.empty() || n->m_meta.label == label) return;
    n->m_meta.label = std::move(label);
    emit(ProjectEventType::NodeChanged, id);
}

void Project::setRasterLocked(NodeId id, bool locked)
{
    Node* n = find(id);
    if (!n || n->m_meta.type != NodeType::Raster) return;
    auto* r = static_cast<RasterRootNode*>(n);
    if (r->locked() == locked) return;
    if (locked && id == m_strokeNode) endStroke();   // finish what is going on before the door closes
    r->setLocked(locked);
    emit(ProjectEventType::NodeChanged, id);
}

void Project::setRasterAlphaLocked(NodeId id, bool locked)
{
    Node* n = find(id);
    if (!n || n->m_meta.type != NodeType::Raster) return;
    auto* r = static_cast<RasterRootNode*>(n);
    if (r->alphaLocked() == locked) return;
    if (id == m_strokeNode) endStroke();   // a stroke keeps the rule it started with
    r->setAlphaLocked(locked);
    emit(ProjectEventType::NodeChanged, id);
}

bool Project::activeRasterEditable() const
{
    const RasterRootNode* r = activeRaster();
    return r && !r->locked();
}

void Project::setActiveRaster(NodeId id)
{
    Node* n = find(id);
    if (id != kNoNodeId && (!n || n->m_meta.type != NodeType::Raster)) return;
    if (m_active == id) return;
    endStroke();   // a stroke can't continue on another raster
    m_active = id;
    emit(ProjectEventType::ActiveRasterChanged, id);
}

bool Project::canConnect(NodeId from, NodeId to, const InputSlot& slot) const
{
    if (from == to || isMaster(from)) return false;   // the master output feeds only the viewport
    NodePtr src = findPtr(from);
    Node* dst = find(to);
    if (!src || !dst || !dst->canDependOn(src)) return false;   // also rules out cycles

    switch (slot.kind) {
    case InputSlot::Kind::Input:
        return dynamic_cast<UnaryNode*>(dst) != nullptr;
    case InputSlot::Kind::Layer: {
        auto* c = dynamic_cast<CompositorNode*>(dst);
        if (!c || slot.layer >= c->layers().size()) return false;
        // a node appears once per stack; re-dropping it on its own slot is fine
        return !c->has(src) || c->layers()[slot.layer] == src;
    }
    case InputSlot::Kind::NewLayer: {   // a layer already in the stack moves there instead
        auto* c = dynamic_cast<CompositorNode*>(dst);
        return c && slot.layer <= c->layers().size();
    }
    case InputSlot::Kind::Mask:
        return dst->hasSignal(slot.signal);   // Dry/Wet masks only on effect nodes
    }
    return false;
}

bool Project::connect(NodeId from, NodeId to, const InputSlot& slot)
{
    if (!canConnect(from, to, slot)) return false;
    NodePtr src = findPtr(from);
    Node* dst = find(to);

    switch (slot.kind) {
    case InputSlot::Kind::Input:
        static_cast<UnaryNode*>(dst)->setInput(src);
        break;
    case InputSlot::Kind::Layer: {
        auto* c = static_cast<CompositorNode*>(dst);
        NodePtr old = c->layers()[slot.layer];   // copy: replaceInputInstance edits the stack
        c->replaceInputInstance(old, src);
        break;
    }
    case InputSlot::Kind::NewLayer: {
        auto* c = static_cast<CompositorNode*>(dst);
        const auto& layers = c->layers();
        if (const auto it = std::find(layers.begin(), layers.end(), src); it != layers.end()) {
            const size_t from = size_t(it - layers.begin());
            const size_t at = slot.layer > from ? slot.layer - 1 : slot.layer;   // it leaves its own place first
            if (at == from) return true;
            c->moveLayer(from, at);
        } else {
            c->addLayerAt(src, slot.layer);
        }
        break;
    }
    case InputSlot::Kind::Mask:
        dst->linkMask(src, slot.signal, slot.channel);
        break;
    }
    emit(ProjectEventType::GraphChanged, to);
    return true;
}

bool Project::disconnect(NodeId to, const InputSlot& slot)
{
    if (!inputAt(to, slot)) return false;
    Node* dst = find(to);
    switch (slot.kind) {
    case InputSlot::Kind::Input: static_cast<UnaryNode*>(dst)->setInput(nullptr); break;
    case InputSlot::Kind::Layer: static_cast<CompositorNode*>(dst)->removeLayer(slot.layer); break;
    case InputSlot::Kind::NewLayer: return false;   // inputAt() already said no
    case InputSlot::Kind::Mask:  dst->linkMask(nullptr, slot.signal, slot.channel); break;
    }
    emit(ProjectEventType::GraphChanged, to);
    return true;
}

bool Project::moveLayer(NodeId compositor, size_t from, size_t to)
{
    auto* c = dynamic_cast<CompositorNode*>(find(compositor));
    if (!c || from >= c->layers().size() || to >= c->layers().size() || from == to) return false;
    c->moveLayer(from, to);
    emit(ProjectEventType::GraphChanged, compositor);
    return true;
}

void Project::setSignalScale(NodeId id, NodeSignal s, NodeChannel c, float v)
{
    Node* n = find(id);
    if (!n || !n->hasSignal(s) || n->signal(s).scale[int(c)] == v) return;
    n->setScale(s, c, v);
    emit(ProjectEventType::NodeChanged, id);
}

void Project::setMoveOffset(NodeId id, int x, int y)
{
    auto* m = dynamic_cast<MoveNode*>(find(id));
    if (!m || !TileUtils::inWorld(x) || !TileUtils::inWorld(y)) return;
    if (m->offsetX() == x && m->offsetY() == y) return;
    m->setOffset(x, y);
    emit(ProjectEventType::NodeChanged, id);
}

void Project::previewEdit(const EditPreview& e)
{
    if (!find(e.node) || e.kind == EditPreview::Kind::None) return;
    m_preview = e;
    emit(ProjectEventType::PreviewChanged, e.node);
}

void Project::applyPreview()
{
    if (!m_preview.active()) return;
    const EditPreview e = std::exchange(m_preview, EditPreview{});
    switch (e.kind) {
    case EditPreview::Kind::SignalScale: setSignalScale(e.node, e.signal, e.channel, e.scale); break;
    case EditPreview::Kind::MoveOffset:  setMoveOffset(e.node, e.x, e.y); break;
    case EditPreview::Kind::None:        break;
    }
    emit(ProjectEventType::PreviewChanged, e.node);
}

void Project::cancelPreview()
{
    if (!m_preview.active()) return;
    const NodeId n = m_preview.node;
    m_preview = {};
    emit(ProjectEventType::PreviewChanged, n);
}

// ---------------------------------------------------------------- raster edits

void Project::beginStroke(const BrushParams& params, StrokePoint at)
{
    if (!activeRasterEditable()) return;
    endStroke();
    BrushParams p = params;
    p.keepAlpha = activeRaster()->alphaLocked();
    m_strokeNode = m_active;
    m_stroke.begin(p, at);
    applyStrokeChanges();
}

void Project::strokeTo(std::span<const StrokePoint> path)
{
    if (!m_stroke.active() || path.empty()) return;
    PerfScope perf(Perf::Stroke);
    for (const StrokePoint& p : path) m_stroke.moveTo(p);
    applyStrokeChanges();
}

void Project::endStroke()
{
    if (!m_stroke.active()) return;
    m_stroke.end();
    RasterRootNode* r = strokeRaster();
    m_strokeNode = kNoNodeId;
    if (!r) return;
    const RasterCommitId commit = r->commitDraft();
    if (commit != kNoRasterCommit && m_history.push({ { { r->raster().id(), commit } } }))
        emit(ProjectEventType::HistoryChanged);
}

RasterRootNode* Project::strokeRaster() const
{
    return static_cast<RasterRootNode*>(find(m_strokeNode));   // only ever names a raster root
}

void Project::applyStrokeChanges()
{
    PerfScope perf(Perf::Stroke);
    const TileSet tiles = m_stroke.takeDirty();
    RasterRootNode* r = strokeRaster();
    if (tiles.empty() || !r) return;
    r->draft([&](RasterData& raster) {
        applyStroke(raster, m_stroke, tiles);
        return tiles;
    });
    emit(ProjectEventType::RasterEdited, m_strokeNode);
}

size_t Project::historyBytes() const
{
    size_t n = 0;
    for (const RasterData& r : m_rasters) n += r.history().bytes();
    return n;
}

bool Project::undo()
{
    if (!canUndo()) return false;
    m_history.undo([&](const RasterCommitRef& c) { flipRasterCommit(c); });
    emit(ProjectEventType::HistoryChanged);
    return true;
}

bool Project::redo()
{
    if (!canRedo()) return false;
    m_history.redo([&](const RasterCommitRef& c) { flipRasterCommit(c); });
    emit(ProjectEventType::HistoryChanged);
    return true;
}

void Project::flipRasterCommit(const RasterCommitRef& c)
{
    if (RasterRootNode* n = rasterNode(c.raster)) {
        n->flip(c.commit);
        emit(ProjectEventType::RasterEdited, n->m_meta.id);
    } else {
        (void)m_rasters[c.raster].flip(c.commit);   // its node was deleted: keep the data consistent
    }
}

// ---------------------------------------------------------------- events

int Project::subscribe(Listener l)
{
    const int token = m_nextToken++;
    m_listeners.push_back(std::make_unique<Subscriber>(Subscriber{ token, std::move(l) }));
    return token;
}

void Project::unsubscribe(int token)
{
    // Flagged, not destroyed: it may be the listener running right now. Swept once no emit runs.
    for (auto& s : m_listeners)
        if (s->token == token) s->removed = true;
    if (m_emitting == 0) std::erase_if(m_listeners, [](const auto& s) { return s->removed; });
}

void Project::emit(ProjectEventType t, NodeId n)
{
    const ProjectEvent e{ t, n };
    ++m_emitting;
    const size_t count = m_listeners.size();   // subscribed during this emit: next event on
    for (size_t i = 0; i < count; ++i) {
        Subscriber* s = m_listeners[i].get();
        if (!s->removed) s->listener(e);
    }
    if (--m_emitting == 0) std::erase_if(m_listeners, [](const auto& s) { return s->removed; });
}
