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
    const StackRows now = stackRowsOf(*c);
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

const Artboard* Workspace::artboard(ArtboardId id) const
{
    return m_artboards.find(id);
}

ArtboardId Workspace::addArtboard(const BoundsI& rect)
{
    const ArtboardId id = m_artboards.add(rect);
    if (id == kNoArtboard) return kNoArtboard;
    std::vector<StackGroup> groups = stackSettings(*m_master).groups;   // empty, on top
    const uint32_t top = uint32_t(m_master->layers().size());
    groups.push_back({ id, top, top });
    setMasterGroups(std::move(groups));
    showArtboards();
    emit(WorkspaceEventType::ArtboardsChanged, m_master->id());
    emit(WorkspaceEventType::LayersChanged, m_master->id());
    return id;
}

bool Workspace::setArtboard(const Artboard& a)
{
    if (!m_artboards.set(a)) return false;
    showArtboards();
    emit(WorkspaceEventType::ArtboardsChanged, m_master->id());
    return true;
}

bool Workspace::deleteArtboard(ArtboardId id)
{
    if (!m_artboards.remove(id)) return false;
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
    return m_artboards.match(stackSettings(*m_master).groups);
}

void Workspace::showArtboards()
{
    assert(artboardsMatchStack() && "an artboard without its group, or a group without its artboard");
    m_master->setLooks(key(), m_artboards.looks());
}
