// The Workspace's stack rows and artboards: a layer muted in its compositor, rows moved together,
// and the master's artboards (a list here, a group of the master's stack each).

#include "project/workspace.hpp"
#include "project/fits.hpp"

#include <algorithm>
#include <cassert>
#include <string>

bool Workspace::setLayerMuted(NodeId compositor, NodeId layer, bool muted)
{
    Node* c = find(compositor);
    if (!c || c->ports().inputs != InputShape::Stack || !Links::layerIndex(*c, layer)) return false;
    CompositorSettings s = stackSettings(*c);
    if (s.mutes(layer) == muted) return false;
    if (muted) s.muted.insert(std::lower_bound(s.muted.begin(), s.muted.end(), layer), layer);
    else       std::erase(s.muted, layer);
    c->setSettings(key(), s);
    emit(WorkspaceEventType::NodeChanged, compositor);
    return true;
}

bool Workspace::moveStackRows(NodeId compositor, const StackMove& move)
{
    Node* c = find(compositor);
    if (!c || c->ports().inputs != InputShape::Stack) return false;
    if ((!move.groups.empty() || move.into != kNoArtboard) && !isMaster(compositor)) return false;   // only the master has artboards
    StackRows now;
    for (const NodePtr& l : c->links().layers) now.layers.push_back(l->id());
    now.groups = stackSettings(*c).groups;
    const std::optional<StackRows> next = moveRows(now, move);
    if (!next || *next == now) return false;

    // The layers into their new order, one move at a time (each tells exactly the tiles it changes),
    // then the groups as they now are.
    for (size_t i = 0; i < next->layers.size(); ++i)
        if (const size_t from = *Links::layerIndex(*c, next->layers[i]); from != i) c->moveLayer(key(), from, i);
    CompositorSettings s = stackSettings(*c);
    s.groups = next->groups;
    c->setSettings(key(), s);
    emit(WorkspaceEventType::LayersChanged, compositor);
    return true;
}

bool Workspace::addLayer(NodeId source, NodeId compositor, ArtboardId into, size_t at)
{
    const Node* c = find(compositor);
    if (!c || c->ports().inputs != InputShape::Stack || !find(source)) return false;
    const bool in = Links::layerIndex(*c, source).has_value();
    InputSlot top;
    const bool linked = !in && Links::preferredInput(*c, top) && connect(source, compositor, top);
    if (!in && !linked) return false;
    const bool moved = moveStackRows(compositor, { { source }, {}, into, at });
    return linked || moved;
}

bool Workspace::removeStackRows(NodeId compositor, const std::vector<NodeId>& layers, const std::vector<ArtboardId>& artboards)
{
    const Node* c = find(compositor);
    if (!c || c->ports().inputs != InputShape::Stack) return false;
    bool any = false;
    if (isMaster(compositor))
        for (ArtboardId id : artboards) any |= deleteArtboard(id);
    for (NodeId id : layers)   // rows close up as each goes: found anew
        if (const std::optional<size_t> at = Links::layerIndex(*c, id)) any |= disconnect(compositor, InputSlot::stackLayer(uint32_t(*at)));
    return any;
}

// ---------------------------------------------------------------- artboards

Artboard* Workspace::findArtboard(ArtboardId id)
{
    for (Artboard& a : m_artboards)
        if (a.id == id) return &a;
    return nullptr;
}

const Artboard* Workspace::artboard(ArtboardId id) const
{
    return const_cast<Workspace*>(this)->findArtboard(id);
}

ArtboardId Workspace::addArtboard(const BoundsI& rect)
{
    if (!rectFits(rect)) return kNoArtboard;
    m_lastArtboard = ArtboardId(uint32_t(m_lastArtboard) + 1);
    Artboard a;
    a.id   = m_lastArtboard;
    a.rect = rect;
    a.name = "Artboard " + std::to_string(uint32_t(a.id));   // ids never come back: nor do names
    m_artboards.push_back(a);
    std::vector<StackGroup> groups = stackSettings(*m_master).groups;   // empty, on top
    const uint32_t top = uint32_t(m_master->layers().size());
    groups.push_back({ a.id, top, top });
    setMasterGroups(std::move(groups));
    showArtboards();
    emit(WorkspaceEventType::ArtboardsChanged, m_master->id());
    emit(WorkspaceEventType::LayersChanged, m_master->id());
    return a.id;
}

bool Workspace::setArtboard(const Artboard& a)
{
    Artboard* now = findArtboard(a.id);
    if (!now || *now == a || !fits(a)) return false;
    *now = a;
    showArtboards();
    emit(WorkspaceEventType::ArtboardsChanged, m_master->id());
    return true;
}

bool Workspace::deleteArtboard(ArtboardId id)
{
    const Artboard* a = findArtboard(id);
    if (!a) return false;
    m_artboards.erase(m_artboards.begin() + (a - m_artboards.data()));
    std::vector<StackGroup> groups = stackSettings(*m_master).groups;
    std::erase_if(groups, [&](const StackGroup& g) { return g.key == id; });
    setMasterGroups(std::move(groups));
    showArtboards();
    emit(WorkspaceEventType::ArtboardsChanged, m_master->id());
    emit(WorkspaceEventType::LayersChanged, m_master->id());
    return true;
}

bool Workspace::setMasterGroups(std::vector<StackGroup> groups)
{
    CompositorSettings s = stackSettings(*m_master);
    if (s.groups == groups) return false;
    s.groups = std::move(groups);
    m_master->setSettings(key(), s);
    return true;
}

bool Workspace::artboardsMatchStack() const
{
    const std::vector<StackGroup> groups = stackSettings(*m_master).groups;
    if (groups.size() != m_artboards.size()) return false;
    return std::all_of(m_artboards.begin(), m_artboards.end(), [&](const Artboard& a) {
        return std::count_if(groups.begin(), groups.end(), [&](const StackGroup& g) { return g.key == a.id; }) == 1;
    });
}

void Workspace::showArtboards()
{
    assert(artboardsMatchStack() && "an artboard without its group, or a group without its artboard");
    std::vector<GroupLook> looks;
    for (const Artboard& a : m_artboards) looks.push_back({ a.id, a.rect, a.color, !a.visible });
    m_master->setLooks(key(), std::move(looks));
}
