#include "project.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace {

void paintStarterContent(GraphKey key, RasterRootNode& background, RasterRootNode& paint)
{
    background.edit(key, [](RasterEditor& ed) {
        for (int y = 0; y < 192; ++y)   // gradient
            for (int x = 0; x < 256; ++x)
                ed.setPixel(x, y, { uint8_t(x), uint8_t(y * 255 / 191), 160, 255 });
        for (int y = 0; y < 128; ++y)   // checkerboard, half-transparent squares
            for (int x = 0; x < 128; ++x) {
                const bool on = ((x / 8) + (y / 8)) % 2 == 0;
                ed.setPixel(300 + x, y, on ? RGBA{ 230, 230, 230, 255 } : RGBA{ 20, 20, 20, 128 });
            }
    });
    paint.edit(key, [](RasterEditor& ed) {
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
    const NodeId master = createNode(NodeType::Compositor, 320.0f, 120.0f);
    m_master = std::static_pointer_cast<CompositorNode>(findPtr(master));
    m_master->setLabel(key(), "Master");
    m_master->enableCache(key(), true);   // the viewport reads its output tile by tile

    const NodeId bg    = createNode(NodeType::Raster, 40.0f, 0.0f);
    const NodeId paint = createNode(NodeType::Raster, 40.0f, 370.0f);
    paintStarterContent(key(), *rasterAt(bg), *rasterAt(paint));   // not undoable
    connect(bg,    master, InputSlot::newLayer(0));
    connect(paint, master, InputSlot::newLayer(1));
    setActiveRaster(paint);
    m_history.setGraph(kStartStep, std::make_shared<const GraphState>(GraphState::capture(m_nodes)));   // where undo can go back to
    m_historyRevision = m_graphRevision;
    m_canvasCamera.zoom = 2.0f;   // the starter content, in view
    m_canvasCamera.pan  = { 300.0f, 160.0f };
}

// ---------------------------------------------------------------- read

RasterRootNode* Project::activeRaster() const
{
    return rasterAt(m_active);
}

RasterRootNode* Project::rasterAt(NodeId id) const
{
    Node* n = find(id);
    return n && n->type() == NodeType::Raster ? static_cast<RasterRootNode*>(n) : nullptr;
}

NodePtr Project::inputAt(NodeId to, const InputSlot& slot) const
{
    const Node* n = find(to);
    return n ? Links::inputAt(*n, slot) : nullptr;
}

bool Project::preferredInput(NodeId id, InputSlot& out) const
{
    const Node* n = find(id);
    return n && Links::preferredInput(*n, out);
}

// ---------------------------------------------------------------- graph edits

NodeId Project::createNode(NodeType type, float x, float y)
{
    const NodeId id = m_nodes.create(key(), type, x, y);
    emit(ProjectEventType::NodeAdded, id);
    if (m_active == kNoNodeId && rasterAt(id)) setActiveRaster(id);
    return id;
}

NodeId Project::importTiles(const TileGrid& tiles, const std::string& name, const ImportOptions& options)
{
    float x = options.nodeX, y = options.nodeY;
    if (!options.placeNode) {   // below the lowest raster in the graph
        x = 40.0f;
        y = 0.0f;
        for (const NodePtr& n : nodes())
            if (n->type() == NodeType::Raster) y = std::max(y, n->y() + 370.0f);
    }
    const NodeId prevActive = m_active;
    const NodeId id = createNode(NodeType::Raster, x, y);
    RasterRootNode& r = *rasterAt(id);
    if (!name.empty()) r.setLabel(key(), name);
    r.edit(key(), [&](RasterEditor& ed) { ed.applyTileGrid(tiles, { 255, CompositeMode::Replace }); });
    if (options.addToMaster) connect(id, m_master->id(), InputSlot::newLayer(uint32_t(m_master->layers().size())));
    if (options.makeActive) setActiveRaster(id);
    else if (m_active == id && prevActive != kNoNodeId) setActiveRaster(prevActive);   // createNode takes it when there was none
    return id;
}

bool Project::deleteNodes(const std::vector<NodeId>& ids)
{
    absl::flat_hash_set<NodeId> doomed;
    for (NodeId id : ids)
        if (!isMaster(id) && find(id)) doomed.insert(id);
    if (doomed.empty()) return false;
    if (previewTouches(doomed)) cancelPreview();
    if (doomed.contains(m_stroke.node())) endStroke();   // committed while its node is still there

    // Every node's inputs before, to tell what changed once it's done.
    absl::flat_hash_map<NodeId, NodeInputs> before;
    for (const NodePtr& n : nodes()) before.emplace(n->id(), NodeInputs::of(*n));

    // Re-feed every surviving slot a doomed node fed, before anything is removed (indices stay valid).
    for (const NodePtr& n : nodes())
        if (!doomed.contains(n->id())) Links::heal(key(), *n, doomed);

    // Whatever wasn't re-fed is dropped here. A raster's pixels stay: history may bring them back.
    for (NodeId id : doomed) {
        find(id)->removeFromGraph(key());   // users lose it (and are told), it drops its inputs and masks
        m_nodes.remove(id);
    }

    // Done: the links that went or were healed, then the nodes.
    const EmitEvent tell = emitter();
    for (NodeId id : doomed) {
        NodeInputs& was = before[id];
        was.layers.clear();   // a stack goes with its node
        Links::emitChanges(id, was, NodeInputs{}, tell);
    }
    for (const NodePtr& n : nodes()) Links::emitChanges(n->id(), before[n->id()], NodeInputs::of(*n), tell);
    for (NodeId id : doomed) emit(ProjectEventType::NodeRemoved, id);

    if (doomed.contains(m_active)) fallBackActiveRaster();
    return true;
}

void Project::fallBackActiveRaster()
{
    NodeId next = kNoNodeId;
    for (auto r = nodes().rbegin(); r != nodes().rend(); ++r)
        if ((*r)->type() == NodeType::Raster) { next = (*r)->id(); break; }
    m_active = kNoNodeId;
    setActiveRaster(next);
    if (next == kNoNodeId) emit(ProjectEventType::ActiveRasterChanged);
}

bool Project::setNodePosition(NodeId id, float x, float y)
{
    Node* n = find(id);
    if (!n || (n->x() == x && n->y() == y)) return false;
    n->setPosition(key(), x, y);
    emit(ProjectEventType::NodeMoved, id);
    return true;
}

bool Project::setNodeLabel(NodeId id, std::string label)
{
    Node* n = find(id);
    if (!n || label.empty() || n->label() == label) return false;
    n->setLabel(key(), std::move(label));
    emit(ProjectEventType::NodeChanged, id);
    return true;
}

bool Project::setNodeSettings(NodeId id, const NodeSettings& settings)
{
    Node* n = find(id);
    if (!n) return false;
    const NodeSettings now = n->settings();
    if (now.index() != settings.index() || now == settings) return false;   // its own kind's, and a change
    if (const auto* m = std::get_if<MoveSettings>(&settings); m && !(TileUtils::inWorld(m->x) && TileUtils::inWorld(m->y))) return false;
    if (id == m_stroke.node()) endStroke();   // finished under the rules it started with
    n->setSettings(key(), settings);
    emit(ProjectEventType::NodeChanged, id);
    return true;
}

bool Project::activeRasterEditable() const
{
    const RasterRootNode* r = activeRaster();
    return r && !r->locked();
}

bool Project::setActiveRaster(NodeId id)
{
    if (id != kNoNodeId && !rasterAt(id)) return false;
    if (m_active == id) return false;
    endStroke();   // a stroke can't continue on another raster
    m_active = id;
    emit(ProjectEventType::ActiveRasterChanged, id);
    return true;
}

bool Project::canConnect(NodeId from, NodeId to, const InputSlot& slot) const
{
    if (from == to || isMaster(from)) return false;   // the master output feeds only the viewport
    const Node* dst = find(to);
    return dst && Links::canConnect(findPtr(from), *dst, slot);
}

bool Project::connect(NodeId from, NodeId to, const InputSlot& slot)
{
    if (!canConnect(from, to, slot)) return false;
    Node& dst = *find(to);
    const NodeInputs before = NodeInputs::of(dst);
    Links::connect(key(), findPtr(from), dst, slot);
    emitInputChanges(to, before);
    return true;
}

bool Project::disconnect(NodeId to, const InputSlot& slot)
{
    Node* dst = find(to);
    if (!dst || !Links::inputAt(*dst, slot)) return false;
    const NodeInputs before = NodeInputs::of(*dst);
    Links::disconnect(key(), *dst, slot);
    emitInputChanges(to, before);
    return true;
}

bool Project::moveLayer(NodeId compositor, size_t from, size_t to)
{
    Node* n = find(compositor);
    if (!n || inputShape(*n) != InputShape::Stack) return false;
    auto* c = static_cast<CompositorNode*>(n);
    if (from >= c->layers().size() || to >= c->layers().size() || from == to) return false;
    c->moveLayer(key(), from, to);
    emit(ProjectEventType::LayersChanged, compositor);
    return true;
}

bool Project::setSignalScale(NodeId id, NodeSignal s, NodeChannel c, float v)
{
    Node* n = find(id);
    if (!n || !n->hasSignal(s) || n->signal(s).scale[int(c)] == v) return false;
    n->setScale(key(), s, c, v);
    emit(ProjectEventType::NodeChanged, id);
    return true;
}

// ---------------------------------------------------------------- interactive edits

bool Project::previewEdit(const EditPreview& e)
{
    // Checked here, at the document's edge: what's previewed is what would be applied.
    bool fits = false;
    if (const auto* s = e.as<ScaleEdit>()) {
        const Node* n = find(s->node);
        fits = n && n->hasSignal(s->signal) && s->scale >= 0.0f && s->scale <= 1.0f;
    } else if (const auto* o = e.as<OffsetEdit>()) {
        const Node* n = find(o->node);
        fits = n && std::holds_alternative<MoveSettings>(n->settings()) && TileUtils::inWorld(o->x) && TileUtils::inWorld(o->y);
    } else if (const auto* m = e.as<NodesMoveEdit>()) {
        fits = std::isfinite(m->dx) && std::isfinite(m->dy) && !m->nodes.empty() &&
               std::all_of(m->nodes.begin(), m->nodes.end(), [&](NodeId id) { return find(id) != nullptr; });
    } else if (const auto* l = e.as<LinkMoveEdit>()) {
        const NodePtr fed = inputAt(l->from, l->slot);
        fits = fed && fed->id() == l->source;
    }   // a stroke is begun with beginStroke()
    if (!fits) return false;
    endStroke();
    m_preview = e;
    emit(ProjectEventType::PreviewChanged, e.pixelsNode());
    return true;
}

void Project::applyPreview()
{
    if (!m_preview || m_preview->stroke()) return;   // a stroke ends with endStroke()
    const EditPreview e = *std::exchange(m_preview, std::nullopt);
    if (const auto* s = e.as<ScaleEdit>())  setSignalScale(s->node, s->signal, s->channel, s->scale);
    if (const auto* o = e.as<OffsetEdit>()) setNodeSettings(o->node, MoveSettings{ o->x, o->y });
    if (const auto* m = e.as<NodesMoveEdit>())
        for (NodeId id : m->nodes)
            if (const Node* n = find(id)) setNodePosition(id, n->x() + m->dx, n->y() + m->dy);
    // A lifted link lands where link.move drops it: it cancels the preview and makes the change.
    emit(ProjectEventType::PreviewChanged, e.pixelsNode());
}

void Project::cancelPreview()
{
    if (!m_preview || m_preview->stroke()) return;
    const NodeId n = m_preview->pixelsNode();
    m_preview.reset();
    emit(ProjectEventType::PreviewChanged, n);
}

bool Project::previewTouches(const absl::flat_hash_set<NodeId>& nodes) const
{
    if (!m_preview) return false;
    if (nodes.contains(m_preview->pixelsNode())) return true;
    if (const auto* m = m_preview->as<NodesMoveEdit>())
        return std::any_of(m->nodes.begin(), m->nodes.end(), [&](NodeId id) { return nodes.contains(id); });
    if (const auto* l = m_preview->as<LinkMoveEdit>()) return nodes.contains(l->source) || nodes.contains(l->from);
    return false;
}

// ---------------------------------------------------------------- raster edits

void Project::beginStroke(const BrushParams& params, StrokePoint at)
{
    if (!activeRasterEditable()) return;
    endStroke();
    applyPreview();   // a drag still going (there's one pointer): kept
    if (!m_stroke.begin(std::static_pointer_cast<RasterRootNode>(findPtr(m_active)), params, at)) return;
    m_preview = EditPreview{ StrokeEdit{ m_active, m_stroke.revision() } };
    emit(ProjectEventType::PreviewChanged, m_active);
}

void Project::strokeTo(std::span<const StrokePoint> path)
{
    if (!m_stroke.moveTo(path)) return;
    m_preview = EditPreview{ StrokeEdit{ m_stroke.node(), m_stroke.revision() } };
    emit(ProjectEventType::PreviewChanged, m_stroke.node());
}

void Project::endStroke()
{
    if (!m_stroke.active()) return;
    const NodeId raster = m_stroke.node();
    const RasterCommitRef commit = m_stroke.end(key());   // the raster changes now, once
    m_preview.reset();
    emit(ProjectEventType::PreviewChanged, raster);
    if (commit.commit == kNoRasterCommit) return;
    emit(ProjectEventType::RasterEdited, raster);
    const bool own = m_stepDepth == 0;   // ended outside any action: a step of its own
    if (own) beginStep("Brush Stroke");
    m_stepCommits.push_back(commit);
    if (own) endStep();
}

// ---------------------------------------------------------------- history

void Project::beginStep(std::string label)
{
    if (m_stepDepth++ > 0) return;   // inside another: merges into it
    m_stepLabel = std::move(label);
    m_stepCommits.clear();
    // Changed outside any step since (nothing should): undo comes back to what it really was.
    const StepId here = m_history.current();
    if (m_graphRevision != m_historyRevision || !m_history.step(here).graph) {
        auto now = std::make_shared<const GraphState>(GraphState::capture(m_nodes));
        const auto& was = m_history.step(here).graph;
        if (!was || !(*was == *now)) m_history.setGraph(here, std::move(now));
        m_historyRevision = m_graphRevision;
    }
    m_stepBefore   = m_history.step(here).graph;
    m_stepRevision = m_graphRevision;
}

void Project::endStep()
{
    if (m_stepDepth == 0 || --m_stepDepth > 0) return;
    // Captured again only if a graph event came: otherwise it's the state it opened with, shared.
    std::shared_ptr<const GraphState> after = std::move(m_stepBefore);
    bool graphChanged = false;
    if (m_graphRevision != m_stepRevision) {
        auto now = std::make_shared<const GraphState>(GraphState::capture(m_nodes));
        graphChanged = !(*now == *after);
        if (graphChanged) after = std::move(now);
    }
    m_historyRevision = m_graphRevision;   // `after` is the live graph, and the current step's from here on
    if (m_stepCommits.empty() && !graphChanged) return;   // changed nothing: undo never "does nothing"
    m_history.push({ std::move(m_stepLabel), std::move(m_stepCommits), std::move(after) });
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

bool Project::goToHistory(StepId step)
{
    if (m_stroke.active() || m_stepDepth > 0 || !m_history.contains(step) || step == m_history.current()) return false;
    cancelPreview();
    m_history.goTo(step, [&](const RasterCommitRef& c) { flipRasterCommit(c); });   // the pixels, step by step
    if (const auto& graph = m_history.step(step).graph) restoreGraph(*graph);    // the graph, as it was there
    m_historyRevision = m_graphRevision;   // the live graph is that step's
    emit(ProjectEventType::HistoryChanged);
    return true;
}

void Project::restoreGraph(const GraphState& target)
{
    target.restore(key(), m_nodes, emitter());
    const bool anyRaster = std::any_of(nodes().begin(), nodes().end(), [](const NodePtr& n) { return n->type() == NodeType::Raster; });
    if (m_active != kNoNodeId ? !find(m_active) : anyRaster) fallBackActiveRaster();
}

void Project::flipRasterCommit(const RasterCommitRef& c)
{
    if (RasterRootNode* n = m_nodes.rasterNode(c.raster)) {
        n->flip(key(), c.commit);
        emit(ProjectEventType::RasterEdited, n->id());
    } else {
        (void)m_nodes.raster(c.raster).flip(c.commit);   // its node was deleted: keep the data consistent
    }
}

// ---------------------------------------------------------------- shared by the views

bool Project::setSelection(NodeSelection selection)
{
    NodeSelection::Set ids;
    for (NodeId id : selection.ids())
        if (find(id)) ids.insert(id);
    const NodeId anchor = find(selection.anchor()) ? selection.anchor() : kNoNodeId;
    NodeSelection next(std::move(ids), anchor);
    if (next == m_selection) return false;   // a rubber band sends the same one every frame it's dragged
    m_selection = std::move(next);
    emit(ProjectEventType::SelectionChanged);
    return true;
}

bool Project::setCanvasCamera(const CanvasCamera& camera)
{
    if (camera.pan == m_canvasCamera.pan && camera.zoom == m_canvasCamera.zoom && camera.rotation == m_canvasCamera.rotation) return false;
    m_canvasCamera = camera;
    emit(ProjectEventType::CanvasViewChanged);
    return true;
}

bool Project::setCanvasViewport(Vec2 size)
{
    if (size == m_canvasViewport) return false;
    m_canvasViewport = size;
    emit(ProjectEventType::CanvasViewChanged);
    return true;
}

// ---------------------------------------------------------------- events

void Project::emit(const ProjectEvent& e)
{
    if (changesGraph(e.type)) ++m_graphRevision;
    changed.emit(e);
    // A node that goes leaves the selection, told once it's gone.
    if (e.type == ProjectEventType::NodeRemoved && (m_selection.contains(e.node) || m_selection.anchor() == e.node)) {
        m_selection = m_selection.without(e.node);
        emit(ProjectEventType::SelectionChanged);
    }
}

void Project::emitInputChanges(NodeId to, const NodeInputs& before)
{
    if (const Node* n = find(to)) Links::emitChanges(to, before, NodeInputs::of(*n), emitter());
}
