#include "workspace.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>
#include <utility>

void Workspace::init(const std::function<void(Workspace&)>& seed)
{
    const NodeId master = createNode(NodeType::Compositor, 320.0f, 120.0f);
    m_master = std::static_pointer_cast<CompositorNode>(findPtr(master));
    m_master->setLabel(key(), "Master");
    m_master->enableCache(key(), true);   // the viewport reads its output tile by tile
    if (seed) {
        m_seeding = true;
        seed(*this);
        m_seeding = false;
    }
    m_steps.start();
}

// ---------------------------------------------------------------- read

RasterRootNode* Workspace::activeRaster() const
{
    return rasterAt(m_active);
}

RasterRootNode* Workspace::rasterAt(NodeId id) const
{
    Node* n = find(id);
    return n && n->type() == NodeType::Raster ? static_cast<RasterRootNode*>(n) : nullptr;
}

NodePtr Workspace::inputAt(NodeId to, const InputSlot& slot) const
{
    const Node* n = find(to);
    return n ? n->linkAt(slot) : nullptr;
}

bool Workspace::preferredInput(NodeId id, InputSlot& out) const
{
    const Node* n = find(id);
    return n && Links::preferredInput(*n, out);
}

// ---------------------------------------------------------------- graph edits

NodeId Workspace::createNode(NodeType type, float x, float y)
{
    const NodeId id = m_nodes.create(key(), type, x, y);
    emit(WorkspaceEventType::NodeAdded, id);
    if (m_active == kNoNodeId && rasterAt(id)) setActiveRaster(id);
    return id;
}

NodeId Workspace::addNode(NodeType type, float x, float y, const NodeWiring& wiring)
{
    // What can be told without the node first: the ends are there, and something can feed it.
    if (wiring.from != kNoNodeId && (!find(wiring.from) || isMaster(wiring.from) || nodeKind(type).ports.inputs == InputShape::None)) return kNoNodeId;
    if (wiring.into != kNoNodeId && !find(wiring.into)) return kNoNodeId;
    const NodeId id = createNode(type, x, y);
    InputSlot in;
    bool wired = wiring.from == kNoNodeId || (preferredInput(id, in) && connect(wiring.from, id, in));
    wired = wired && (wiring.into == kNoNodeId || connect(id, wiring.into, wiring.slot));
    if (wired) return id;
    deleteNodes({ id });   // a link it can't have: as if never made
    return kNoNodeId;
}

NodeId Workspace::importTiles(const TileGrid& tiles, const std::string& name, const ImportOptions& options)
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
    recordCommit({ r.raster().id(), r.recordEdit(key(), [&](RasterEditor& ed) { ed.applyTileGrid(tiles, { 255, CompositeMode::Replace }); }) },
                 "Import Image");
    if (options.addToMaster) connect(id, m_master->id(), InputSlot::newLayer(uint32_t(m_master->layers().size())));
    if (options.makeActive) setActiveRaster(id);
    else if (m_active == id && prevActive != kNoNodeId) setActiveRaster(prevActive);   // createNode takes it when there was none
    return id;
}

bool Workspace::deleteNodes(const std::vector<NodeId>& ids)
{
    absl::flat_hash_set<NodeId> doomed;
    for (NodeId id : ids)
        if (!isMaster(id) && find(id)) doomed.insert(id);
    if (doomed.empty()) return false;
    if (previewTouches(doomed)) cancelPreview();
    if (doomed.contains(m_stroke.node())) endStroke();   // committed while its node is still there

    // Every node's inputs before, to tell what changed once it's done.
    absl::flat_hash_map<NodeId, NodeLinks> before;
    for (const NodePtr& n : nodes()) before.emplace(n->id(), n->links());

    // Re-feed every surviving slot a doomed node fed, before anything is removed (indices stay valid).
    for (const NodePtr& n : nodes())
        if (!doomed.contains(n->id())) Links::heal(key(), *n, doomed);

    // Whatever wasn't re-fed is dropped here. A raster's pixels stay: history may bring them back.
    for (NodeId id : doomed) {
        find(id)->removeFromGraph(key());   // users lose it (and are told), it drops what fed it
        m_nodes.remove(id);
    }

    // Done: the links that went or were healed, then the nodes.
    const EmitEvent tell = emitter();
    for (NodeId id : doomed) {
        NodeLinks& was = before[id];
        was.layers.clear();   // a stack goes with its node
        Links::emitChanges(id, was, NodeLinks{}, tell);
    }
    for (const NodePtr& n : nodes()) Links::emitChanges(n->id(), before[n->id()], n->links(), tell);
    for (NodeId id : doomed) emit(WorkspaceEventType::NodeRemoved, id);

    if (doomed.contains(m_active)) fallBackActiveRaster();
    return true;
}

void Workspace::fallBackActiveRaster()
{
    NodeId next = kNoNodeId;
    for (auto r = nodes().rbegin(); r != nodes().rend(); ++r)
        if ((*r)->type() == NodeType::Raster) { next = (*r)->id(); break; }
    m_active = kNoNodeId;
    setActiveRaster(next);
    if (next == kNoNodeId) emit(WorkspaceEventType::ActiveRasterChanged);
}

bool Workspace::setNodePosition(NodeId id, float x, float y)
{
    Node* n = find(id);
    if (!n || (n->x() == x && n->y() == y)) return false;
    n->setPosition(key(), x, y);
    emit(WorkspaceEventType::NodeMoved, id);
    return true;
}

bool Workspace::setNodeLabel(NodeId id, std::string label)
{
    Node* n = find(id);
    if (!n || label.empty() || n->label() == label) return false;
    n->setLabel(key(), std::move(label));
    emit(WorkspaceEventType::NodeChanged, id);
    return true;
}

bool Workspace::setNodeSettings(NodeId id, const NodeSettings& settings)
{
    Node* n = find(id);
    if (!n) return false;
    const NodeSettings now = n->settings();
    if (now.index() != settings.index() || now == settings) return false;   // its own kind's, and a change
    if (const auto* m = std::get_if<MoveSettings>(&settings); m && !(TileUtils::inWorld(m->x) && TileUtils::inWorld(m->y))) return false;
    if (const auto* c = std::get_if<CompositorSettings>(&settings); c && c->groups != std::get<CompositorSettings>(now).groups)
        return false;   // the artboards' (addArtboard, moveStackRows)
    if (id == m_stroke.node()) endStroke();   // finished under the rules it started with
    n->setSettings(key(), settings);
    emit(WorkspaceEventType::NodeChanged, id);
    return true;
}

bool Workspace::activeRasterEditable() const
{
    const RasterRootNode* r = activeRaster();
    return r && !r->locked();
}

bool Workspace::setActiveRaster(NodeId id)
{
    if (id != kNoNodeId && !rasterAt(id)) return false;
    if (m_active == id) return false;
    endStroke();   // a stroke can't continue on another raster
    m_active = id;
    emit(WorkspaceEventType::ActiveRasterChanged, id);
    return true;
}

bool Workspace::canConnect(NodeId from, NodeId to, const InputSlot& slot) const
{
    if (from == to || isMaster(from)) return false;   // the master output feeds only the viewport
    const Node* dst = find(to);
    return dst && dst->canLink(findPtr(from), slot);
}

bool Workspace::connect(NodeId from, NodeId to, const InputSlot& slot)
{
    if (!canConnect(from, to, slot)) return false;
    Node& dst = *find(to);
    const NodeLinks before = dst.links();
    dst.link(key(), findPtr(from), slot);
    emitInputChanges(to, before);
    return true;
}

bool Workspace::disconnect(NodeId to, const InputSlot& slot)
{
    Node* dst = find(to);
    if (!dst || !dst->linkAt(slot)) return false;
    const NodeLinks before = dst->links();
    dst->unlink(key(), slot);
    emitInputChanges(to, before);
    return true;
}

bool Workspace::canMoveLink(NodeId source, NodeId from, const InputSlot& slot, NodeId to, const InputSlot& toSlot) const
{
    const Node* f = find(from);
    const NodePtr src = findPtr(source);
    if (!f || !src || f->linkAt(slot) != src) return false;
    if (to == kNoNodeId) return true;   // off
    const Node* t = find(to);
    return t && Links::canMove(src, *f, slot, *t, toSlot);
}

bool Workspace::moveLink(NodeId source, NodeId from, const InputSlot& slot, NodeId to, const InputSlot& toSlot)
{
    if (!canMoveLink(source, from, slot, to, toSlot)) return false;
    if (to == from && toSlot == slot) return false;   // back where it was: nothing changes
    disconnect(from, slot);
    if (to != kNoNodeId) {
        [[maybe_unused]] const bool landed = connect(source, to, toSlot);
        assert(landed && "canMove said it lands");
    }
    return true;
}

bool Workspace::setSignalScale(NodeId id, NodeSignal s, NodeChannel c, float v)
{
    Node* n = find(id);
    if (!n || !n->hasSignal(s) || !(v >= 0.0f && v <= 1.0f) || n->signal(s).scale[int(c)] == v) return false;   // 0..1 (NaN isn't)
    n->setScale(key(), s, c, v);
    emit(WorkspaceEventType::NodeChanged, id);
    return true;
}

// ---------------------------------------------------------------- interactive edits

bool Workspace::previewEdit(const EditPreview& e)
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
    emit(WorkspaceEventType::PreviewChanged, e.pixelsNode());
    return true;
}

void Workspace::applyPreview()
{
    if (!m_preview || m_preview->stroke()) return;   // a stroke ends with endStroke()
    const EditPreview e = *std::exchange(m_preview, std::nullopt);
    if (const auto* s = e.as<ScaleEdit>())  setSignalScale(s->node, s->signal, s->channel, s->scale);
    if (const auto* o = e.as<OffsetEdit>()) setNodeSettings(o->node, MoveSettings{ o->x, o->y });
    if (const auto* m = e.as<NodesMoveEdit>())
        for (NodeId id : m->nodes)
            if (const Node* n = find(id)) setNodePosition(id, n->x() + m->dx, n->y() + m->dy);
    // A lifted link lands where link.move drops it: it cancels the preview and makes the change.
    emit(WorkspaceEventType::PreviewChanged, e.pixelsNode());
}

void Workspace::cancelPreview()
{
    if (!m_preview || m_preview->stroke()) return;
    const NodeId n = m_preview->pixelsNode();
    m_preview.reset();
    emit(WorkspaceEventType::PreviewChanged, n);
}

bool Workspace::previewTouches(const absl::flat_hash_set<NodeId>& nodes) const
{
    if (!m_preview) return false;
    if (nodes.contains(m_preview->pixelsNode())) return true;
    if (const auto* m = m_preview->as<NodesMoveEdit>())
        return std::any_of(m->nodes.begin(), m->nodes.end(), [&](NodeId id) { return nodes.contains(id); });
    if (const auto* l = m_preview->as<LinkMoveEdit>()) return nodes.contains(l->source) || nodes.contains(l->from);
    return false;
}

// ---------------------------------------------------------------- raster edits

void Workspace::beginStroke(const BrushParams& params, StrokePoint at)
{
    if (!activeRasterEditable()) return;
    endStroke();
    applyPreview();   // a drag still going (there's one pointer): kept
    if (!m_stroke.begin(std::static_pointer_cast<RasterRootNode>(findPtr(m_active)), params, at)) return;
    m_preview = EditPreview{ StrokeEdit{ m_active, m_stroke.revision() } };
    emit(WorkspaceEventType::PreviewChanged, m_active);
}

void Workspace::strokeTo(std::span<const StrokePoint> path)
{
    if (!m_stroke.moveTo(path)) return;
    m_preview = EditPreview{ StrokeEdit{ m_stroke.node(), m_stroke.revision() } };
    emit(WorkspaceEventType::PreviewChanged, m_stroke.node());
}

void Workspace::endStroke()
{
    if (!m_stroke.active()) return;
    const NodeId raster = m_stroke.node();
    const RasterCommitRef commit = m_stroke.end(key());   // the raster changes now, once
    m_preview.reset();
    emit(WorkspaceEventType::PreviewChanged, raster);
    if (commit.commit == kNoRasterCommit) return;
    emit(WorkspaceEventType::RasterEdited, raster);
    recordCommit(commit, "Brush Stroke");
}

void Workspace::cancelStroke()
{
    if (!m_stroke.active()) return;
    const NodeId raster = m_stroke.node();
    m_stroke.discard();
    m_preview.reset();
    emit(WorkspaceEventType::PreviewChanged, raster);
}

void Workspace::recordCommit(const RasterCommitRef& commit, const char* label)
{
    if (commit.commit == kNoRasterCommit || m_seeding) return;   // a seed's pixels are the start's own
    const bool own = !m_steps.open();   // made outside any action: a step of its own
    if (own) beginStep(label);
    m_steps.commit(commit);
    if (own) endStep();
}

// ---------------------------------------------------------------- history

void Workspace::beginStep(std::string label)
{
    m_steps.begin(std::move(label));
}

void Workspace::endStep()
{
    if (m_steps.end()) emit(WorkspaceEventType::HistoryChanged);
}

bool Workspace::undo()
{
    return canUndo() && goToHistory(history().parent(history().current()));
}

bool Workspace::redo()
{
    return canRedo() && goToHistory(history().redoChild(history().current()));
}

bool Workspace::goToHistory(StepId step)
{
    if (m_stroke.active() || m_steps.open() || !history().contains(step) || step == history().current()) return false;
    cancelPreview();
    const auto graph = m_steps.goTo(step, [&](const RasterCommitRef& c) { flipRasterCommit(c); });   // the pixels, step by step
    if (graph) restoreGraph(*graph);   // the graph, as it was there
    m_steps.synced();                  // the live graph is that step's
    emit(WorkspaceEventType::HistoryChanged);
    return true;
}

void Workspace::restoreGraph(const GraphState& target)
{
    target.restore(key(), m_nodes, emitter());
    const Artboards boards = target.artboards ? *target.artboards : Artboards{};
    if (boards != m_artboards) {
        m_artboards = boards;
        showArtboards();
        emit(WorkspaceEventType::ArtboardsChanged, m_master->id());
    }
    assert(artboardsMatchStack() && "a step holds its artboards and the master's groups together");
    const bool anyRaster = std::any_of(nodes().begin(), nodes().end(), [](const NodePtr& n) { return n->type() == NodeType::Raster; });
    if (m_active != kNoNodeId ? !find(m_active) : anyRaster) fallBackActiveRaster();
}

void Workspace::flipRasterCommit(const RasterCommitRef& c)
{
    RasterRootNode& n = m_nodes.rasterNode(c.raster);   // in the graph or not: its caches follow its pixels
    n.flip(key(), c.commit);
    if (m_nodes.contains(n.id())) emit(WorkspaceEventType::RasterEdited, n.id());
}

// ---------------------------------------------------------------- shared by the views

bool Workspace::setSelection(NodeSelection selection)
{
    NodeSelection::Set ids;
    for (NodeId id : selection.ids())
        if (find(id)) ids.insert(id);
    const NodeId anchor = find(selection.anchor()) ? selection.anchor() : kNoNodeId;
    NodeSelection next(std::move(ids), anchor);
    if (next == m_selection) return false;   // a rubber band sends the same one every frame it's dragged
    m_selection = std::move(next);
    emit(WorkspaceEventType::SelectionChanged);
    return true;
}

bool Workspace::setCanvasCamera(const CanvasCamera& camera)
{
    if (camera == m_canvasCamera) return false;
    m_canvasCamera = camera;
    emit(WorkspaceEventType::CanvasViewChanged);
    return true;
}

bool Workspace::setCanvasViewport(Vec2 size)
{
    if (size == m_canvasViewport) return false;
    m_canvasViewport = size;
    emit(WorkspaceEventType::CanvasViewChanged);
    return true;
}

// ---------------------------------------------------------------- events

void Workspace::emit(const WorkspaceEvent& e)
{
    if (changesGraph(e.type)) m_steps.touched(e.node);
    // A node that goes leaves the selection before anyone hears it went (the state is final when
    // told), then the selection's change is told.
    const bool deselect = e.type == WorkspaceEventType::NodeRemoved && (m_selection.contains(e.node) || m_selection.anchor() == e.node);
    if (deselect) m_selection = m_selection.without(e.node);
    changed.emit(e);
    if (deselect) emit(WorkspaceEventType::SelectionChanged);
}

void Workspace::emitInputChanges(NodeId to, const NodeLinks& before)
{
    if (const Node* n = find(to)) Links::emitChanges(to, before, n->links(), emitter());
}
