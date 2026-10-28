#include "project.hpp"
#include "core/perf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

// ---------------------------------------------------------------- input slots

namespace {

constexpr const char* kSignalNames[]  = { "main", "dry", "wet" };
constexpr const char* kChannelNames[] = { "a", "r", "g", "b" };

}

std::string InputSlot::text() const
{
    switch (kind) {
    case Kind::Input:    return "input";
    case Kind::Layer:    return "layer:" + std::to_string(layer);
    case Kind::NewLayer: return "new:" + std::to_string(layer);
    case Kind::Mask:     return std::string("mask:") + kSignalNames[int(signal)] + "." + kChannelNames[int(channel)];
    }
    return "input";
}

InputSlot InputSlot::parse(std::string_view t)
{
    const auto index = [&](size_t from) { return uint32_t(std::strtoul(std::string(t.substr(from)).c_str(), nullptr, 10)); };
    if (t.starts_with("layer:")) return stackLayer(index(6));
    if (t.starts_with("new:"))   return newLayer(index(4));
    if (t.starts_with("mask:")) {
        const std::string_view m = t.substr(5);
        const size_t dot = m.find('.');
        InputSlot s = mask(NodeSignal::Main, NodeChannel::A);
        for (int i = 0; i < kSignalCount; ++i)  if (m.substr(0, dot) == kSignalNames[i]) s.signal = NodeSignal(i);
        if (dot != std::string_view::npos)
            for (int i = 0; i < kChannelCount; ++i) if (m.substr(dot + 1) == kChannelNames[i]) s.channel = NodeChannel(i);
        return s;
    }
    return input();
}

bool preferredInput(const Node& n, InputSlot& out)
{
    if (dynamic_cast<const UnaryNode*>(&n)) { out = InputSlot::input(); return true; }
    if (auto* c = dynamic_cast<const CompositorNode*>(&n)) { out = InputSlot::newLayer(uint32_t(c->layers().size())); return true; }
    return false;
}

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
    m_history.setGraph(0, std::make_shared<GraphState>(captureGraph()));   // where undo can go back to
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
    const NodeId id = adopt(ReferenceNode::create(t), x, y);
    if (t) emit({ ProjectEventType::LinkAdded, id, t->m_meta.id, InputSlot::input() });
    return id;
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

    // Every node's inputs before, to tell what changed once it's done.
    absl::flat_hash_map<NodeId, Inputs> before;
    for (const NodePtr& n : m_order) before.emplace(n->m_meta.id, inputsOf(*n));

    // Re-feed every surviving slot a doomed node fed, before anything is removed (indices stay valid).
    for (const NodePtr& n : m_order) {
        if (doomed.contains(n->m_meta.id)) continue;
        const auto source = [&](const NodePtr& old) -> NodePtr {
            if (!old || !doomed.contains(old->m_meta.id)) return nullptr;
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
    }

    // Done: the links that went or were healed, then the nodes.
    for (NodeId id : doomed) {
        Inputs& was = before[id];
        was.layers.clear();   // a stack goes with its node
        emitInputChanges(id, was, Inputs{});
    }
    for (const NodePtr& n : m_order) emitInputChanges(n->m_meta.id, before[n->m_meta.id]);
    for (NodeId id : doomed) emit(ProjectEventType::NodeRemoved, id);

    if (doomed.contains(m_active)) fallBackActiveRaster();
    return true;
}

void Project::fallBackActiveRaster()
{
    NodeId next = kNoNodeId;
    for (auto r = m_order.rbegin(); r != m_order.rend(); ++r)
        if ((*r)->m_meta.type == NodeType::Raster) { next = (*r)->m_meta.id; break; }
    m_active = kNoNodeId;
    setActiveRaster(next);
    if (next == kNoNodeId) emit(ProjectEventType::ActiveRasterChanged);
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
    const Inputs before = inputsOf(*dst);

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
    emitInputChanges(to, before);
    return true;
}

bool Project::disconnect(NodeId to, const InputSlot& slot)
{
    if (!inputAt(to, slot)) return false;
    Node* dst = find(to);
    const Inputs before = inputsOf(*dst);
    switch (slot.kind) {
    case InputSlot::Kind::Input: static_cast<UnaryNode*>(dst)->setInput(nullptr); break;
    case InputSlot::Kind::Layer: static_cast<CompositorNode*>(dst)->removeLayer(slot.layer); break;
    case InputSlot::Kind::NewLayer: return false;   // inputAt() already said no
    case InputSlot::Kind::Mask:  dst->linkMask(nullptr, slot.signal, slot.channel); break;
    }
    emitInputChanges(to, before);
    return true;
}

bool Project::moveLayer(NodeId compositor, size_t from, size_t to)
{
    auto* c = dynamic_cast<CompositorNode*>(find(compositor));
    if (!c || from >= c->layers().size() || to >= c->layers().size() || from == to) return false;
    c->moveLayer(from, to);
    emit(ProjectEventType::LayersChanged, compositor);
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
    if (m_preview.node != e.node) endPreviewBounds();
    m_preview = e;
    if (e.kind == EditPreview::Kind::MoveOffset)   // the bounds follow live; pixels wait for applyPreview()
        if (auto* m = dynamic_cast<MoveNode*>(find(e.node))) m->previewOffset(e.x, e.y);
    emit(ProjectEventType::PreviewChanged, e.node);
}

void Project::endPreviewBounds()
{
    if (auto* m = dynamic_cast<MoveNode*>(find(m_preview.node))) m->endPreview();
}

void Project::applyPreview()
{
    if (!m_preview.active()) return;
    endPreviewBounds();
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
    endPreviewBounds();
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
    if (commit == kNoRasterCommit) return;
    const bool own = m_stepDepth == 0;   // ended outside any action: a step of its own
    if (own) beginStep("Brush Stroke");
    m_stepCommits.push_back({ r->raster().id(), commit });
    if (own) endStep();
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

// ---------------------------------------------------------------- history

void Project::beginStep(std::string label)
{
    if (m_stepDepth++ > 0) return;   // inside another: merges into it
    m_stepLabel = std::move(label);
    m_stepCommits.clear();
    m_stepBefore = captureGraph();
    // Changed outside any step since (nothing should): undo comes back to what it really was.
    const auto& here = m_history.step(m_history.current()).graph;
    if (!here || !(*here == m_stepBefore)) m_history.setGraph(m_history.current(), std::make_shared<GraphState>(m_stepBefore));
}

void Project::endStep()
{
    if (m_stepDepth == 0 || --m_stepDepth > 0) return;
    GraphState after = captureGraph();
    if (m_stepCommits.empty() && after == m_stepBefore) return;   // changed nothing: undo never "does nothing"
    m_history.push({ std::move(m_stepLabel), std::move(m_stepCommits), std::make_shared<GraphState>(std::move(after)) });
    m_stepCommits.clear();
    emit(ProjectEventType::HistoryChanged);
}

bool Project::undo()
{
    return canUndo() && goToHistory(m_history.parent(m_history.current()));
}

bool Project::redo()
{
    return canRedo() && goToHistory(m_history.redoChild(m_history.current()));
}

bool Project::goToHistory(uint32_t node)
{
    if (m_stroke.active() || m_stepDepth > 0 || node >= m_history.size() || node == m_history.current()) return false;
    cancelPreview();
    m_history.goTo(node, [&](const RasterCommitRef& c) { flipRasterCommit(c); });   // the pixels, step by step
    if (const auto& graph = m_history.step(node).graph) restoreGraph(*graph);    // the graph, as it was there
    emit(ProjectEventType::HistoryChanged);
    return true;
}

GraphState Project::captureGraph() const
{
    GraphState g;
    g.nodes.reserve(m_order.size());
    for (const NodePtr& n : m_order) {
        NodeState& s = g.nodes.emplace_back();
        s.node  = n;
        s.label = n->m_meta.label;
        s.x = n->m_meta.x;
        s.y = n->m_meta.y;
        for (int sig = 0; sig < kSignalCount; ++sig)
            for (int ch = 0; ch < kChannelCount; ++ch) {
                const SignalChannels& c = n->signal(NodeSignal(sig));
                s.scale[sig][ch] = c.scale[ch];
                s.mask[sig][ch]  = c.mask[ch] ? c.mask[ch]->m_meta.id : kNoNodeId;
            }
        if (auto* u = dynamic_cast<const UnaryNode*>(n.get()); u && u->input()) s.input = u->input()->m_meta.id;
        if (auto* c = dynamic_cast<const CompositorNode*>(n.get()))
            for (const NodePtr& l : c->layers()) s.layers.push_back(l->m_meta.id);
        if (auto* m = dynamic_cast<const MoveNode*>(n.get())) { s.offsetX = m->offsetX(); s.offsetY = m->offsetY(); }
        if (auto* r = dynamic_cast<const RasterRootNode*>(n.get())) { s.locked = r->locked(); s.alphaLocked = r->alphaLocked(); }
    }
    return g;
}

void Project::restoreGraph(const GraphState& target)
{
    absl::flat_hash_map<NodeId, const NodeState*> wanted;
    for (const NodeState& s : target.nodes) wanted.emplace(s.node->m_meta.id, &s);
    const auto nodeOf = [&](NodeId id) -> NodePtr {
        const auto it = wanted.find(id);
        return it == wanted.end() ? nullptr : it->second->node;
    };
    const auto inputsWanted = [&](const NodeState& s) {
        Inputs in;
        in.input = nodeOf(s.input);
        for (NodeId id : s.layers) in.layers.push_back(nodeOf(id));
        for (int sig = 0; sig < kSignalCount; ++sig)
            for (int ch = 0; ch < kChannelCount; ++ch) in.masks[sig][ch] = nodeOf(s.mask[sig][ch]);
        return in;
    };

    // What fed every node before, for the events at the end.
    absl::flat_hash_map<NodeId, Inputs> before;
    for (const NodePtr& n : m_order) before.emplace(n->m_meta.id, inputsOf(*n));

    // Nodes that come back, as themselves: unlinked (they were let go of when they went), told now.
    for (const NodeState& s : target.nodes) {
        const NodeId id = s.node->m_meta.id;
        if (m_nodes.contains(id)) continue;
        m_nodes.emplace(id, s.node);
        m_order.push_back(s.node);
        if (s.node->m_meta.type == NodeType::Raster)
            m_rasterNodes[static_cast<const RasterRootNode*>(s.node.get())->raster().id()] = id;
        before.emplace(id, Inputs{});
        emit(ProjectEventType::NodeAdded, id);
    }

    // Links: every node whose inputs differ lets go of all of them first, then takes the wanted
    // ones, so the graph never passes through a cycle on the way (it only ever holds wanted links).
    std::vector<std::pair<Node*, Inputs>> relink;
    for (const NodePtr& n : m_order) {
        const auto it = wanted.find(n->m_meta.id);
        Inputs want = it != wanted.end() ? inputsWanted(*it->second) : Inputs{};
        if (inputsOf(*n) == want) continue;
        n->detachAllInputs();
        n->unlinkAllMasks();
        relink.emplace_back(n.get(), std::move(want));
    }
    for (auto& [n, in] : relink) {
        if (auto* u = dynamic_cast<UnaryNode*>(n)) u->setInput(in.input);
        if (auto* c = dynamic_cast<CompositorNode*>(n))
            for (const NodePtr& l : in.layers) c->addLayer(l);
        for (int sig = 0; sig < kSignalCount; ++sig)
            for (int ch = 0; ch < kChannelCount; ++ch)
                if (in.masks[sig][ch]) n->linkMask(in.masks[sig][ch], NodeSignal(sig), NodeChannel(ch));
    }

    // Settings, each node type its own.
    std::vector<NodeId> moved, changed;
    for (const NodeState& s : target.nodes) {
        Node& n = *s.node;
        if (n.m_meta.x != s.x || n.m_meta.y != s.y) {
            n.m_meta.x = s.x;
            n.m_meta.y = s.y;
            moved.push_back(n.m_meta.id);
        }
        bool c = false;
        if (n.m_meta.label != s.label) { n.m_meta.label = s.label; c = true; }
        for (int sig = 0; sig < kSignalCount; ++sig)
            for (int ch = 0; ch < kChannelCount; ++ch)
                if (n.hasSignal(NodeSignal(sig)) && n.signal(NodeSignal(sig)).scale[ch] != s.scale[sig][ch]) {
                    n.setScale(NodeSignal(sig), NodeChannel(ch), s.scale[sig][ch]);
                    c = true;
                }
        if (auto* m = dynamic_cast<MoveNode*>(&n); m && (m->offsetX() != s.offsetX || m->offsetY() != s.offsetY)) {
            m->setOffset(s.offsetX, s.offsetY);
            c = true;
        }
        if (auto* r = dynamic_cast<RasterRootNode*>(&n)) {
            if (r->locked() != s.locked)           { r->setLocked(s.locked); c = true; }
            if (r->alphaLocked() != s.alphaLocked) { r->setAlphaLocked(s.alphaLocked); c = true; }
        }
        if (c) changed.push_back(n.m_meta.id);
    }

    // Nodes that go: unlinked above, and nothing uses them any more. History keeps them.
    std::vector<NodeId> removed;
    for (const NodePtr& n : m_order) {
        const NodeId id = n->m_meta.id;
        if (wanted.contains(id)) continue;
        removed.push_back(id);
        m_nodes.erase(id);
        if (n->m_meta.type == NodeType::Raster) absl::erase_if(m_rasterNodes, [&](const auto& kv) { return kv.second == id; });
    }
    m_order.clear();
    for (const NodeState& s : target.nodes) m_order.push_back(s.node);   // creation order

    // Told, once it's all done: links, settings, then the nodes that went.
    for (const NodeState& s : target.nodes) emitInputChanges(s.node->m_meta.id, before[s.node->m_meta.id]);
    for (NodeId id : removed) {
        Inputs& was = before[id];
        was.layers.clear();   // a stack goes with its node
        emitInputChanges(id, was, Inputs{});
    }
    for (NodeId id : moved)   emit(ProjectEventType::NodeMoved, id);
    for (NodeId id : changed) emit(ProjectEventType::NodeChanged, id);
    for (NodeId id : removed) emit(ProjectEventType::NodeRemoved, id);

    const bool anyRaster = std::any_of(m_order.begin(), m_order.end(), [](const NodePtr& n) { return n->m_meta.type == NodeType::Raster; });
    if (m_active != kNoNodeId ? !find(m_active) : anyRaster) fallBackActiveRaster();
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

void Project::emit(const ProjectEvent& e)
{
    ++m_emitting;
    const size_t count = m_listeners.size();   // subscribed during this emit: next event on
    for (size_t i = 0; i < count; ++i) {
        Subscriber* s = m_listeners[i].get();
        if (!s->removed) s->listener(e);
    }
    if (--m_emitting == 0) std::erase_if(m_listeners, [](const auto& s) { return s->removed; });
}

Project::Inputs Project::inputsOf(const Node& n)
{
    Inputs in;
    if (auto* u = dynamic_cast<const UnaryNode*>(&n)) in.input = u->input();
    if (auto* c = dynamic_cast<const CompositorNode*>(&n)) in.layers = c->layers();
    for (int s = 0; s < kSignalCount; ++s)
        for (int ch = 0; ch < kChannelCount; ++ch) in.masks[s][ch] = n.signal(NodeSignal(s)).mask[ch];
    return in;
}

void Project::emitInputChanges(NodeId to, const Inputs& before, const Inputs& now)
{
    const auto single = [&](const NodePtr& was, const NodePtr& is, const InputSlot& slot) {
        if (was == is) return;
        if (was) emit({ ProjectEventType::LinkRemoved, to, was->m_meta.id, slot });
        if (is)  emit({ ProjectEventType::LinkAdded,   to, is->m_meta.id,  slot });
    };
    single(before.input, now.input, InputSlot::input());
    for (int s = 0; s < kSignalCount; ++s)
        for (int ch = 0; ch < kChannelCount; ++ch)
            single(before.masks[s][ch], now.masks[s][ch], InputSlot::mask(NodeSignal(s), NodeChannel(ch)));
    if (before.layers != now.layers) emit(ProjectEventType::LayersChanged, to);
}

void Project::emitInputChanges(NodeId to, const Inputs& before)
{
    if (const Node* n = find(to)) emitInputChanges(to, before, inputsOf(*n));
}
