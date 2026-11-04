#include "ui/panels/canvas/preview/preview.hpp"
#include "ui/panels/canvas/preview/stroke-source.hpp"
#include "document/workspace.hpp"
#include "node/node-kinds.hpp"

void Preview::init(const Workspace& workspace, TileCache& cache)
{
    m_workspace = &workspace;
    m_cache   = &cache;
    m_model.emplace(workspace);
    m_events  = workspace.changed.connect([this](const WorkspaceEvent& e) {
        if (changesGraph(e.type)) m_stale = true;
        if (e.type == WorkspaceEventType::PreviewChanged || m_stale) follow();   // at once: views read it while drawing
    });
    follow();
}

void Preview::shutdown()
{
    m_events.disconnect();
    drop();
    m_model.reset();
    m_workspace = nullptr;
    m_cache   = nullptr;
}

void Preview::releaseGpu()
{
    drop();
    m_stale = true;
}

const EditPreview& Preview::edit() const
{
    return *m_workspace->preview();
}

void Preview::update()
{
    follow();
    for (size_t i = 0; i < m_leaves.size(); ++i)   // the cache syncs its own; the stroke is ours
        if (plan().leaves()[i].kind == PreviewLeaf::Kind::Stroke) m_leaves[i]->sync();
}

void Preview::follow()
{
    const EditPreview* e = m_workspace ? m_workspace->preview() : nullptr;
    if (!e || e->pixelsNode() == kNoNodeId) {   // nothing, or the graph alone
        if (m_active) drop();
        return;
    }
    if (!m_active || m_stale || e->pixelsNode() != plan().edited() || e->edit.index() != m_kind) replan(*e);
}

void Preview::replan(const EditPreview& edit)
{
    drop();
    m_stale = false;
    if (!m_model->plan(edit)) return;
    for (const PreviewLeaf& leaf : plan().leaves()) {
        switch (leaf.kind) {
        case PreviewLeaf::Kind::Output: m_leaves.push_back(m_cache->output(leaf.node)); break;
        case PreviewLeaf::Kind::Wet:    m_leaves.push_back(m_cache->wet(leaf.node)); break;
        case PreviewLeaf::Kind::Stroke: m_leaves.push_back(m_cache->make(std::make_unique<StrokeSource>(*m_workspace))); break;
        }
    }
    m_kind   = edit.edit.index();
    m_active = true;
    ++m_serial;
}

void Preview::drop()
{
    m_leaves.clear();
    if (m_model) m_model->clear();
    m_active = false;
    ++m_serial;
}
