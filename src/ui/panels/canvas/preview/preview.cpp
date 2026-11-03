#include "ui/panels/canvas/preview/preview.hpp"
#include "ui/panels/canvas/preview/stroke-source.hpp"
#include "document/project.hpp"
#include "node/node-kinds.hpp"

void Preview::init(const Project& project, TileCache& cache)
{
    m_project = &project;
    m_cache   = &cache;
    m_model.emplace(project);
    m_events  = project.changed.connect([this](const ProjectEvent& e) {
        if (changesGraph(e.type)) m_stale = true;
        if (e.type == ProjectEventType::PreviewChanged || m_stale) follow();   // at once: views read it while drawing
    });
    follow();
}

void Preview::shutdown()
{
    m_events.disconnect();
    drop();
    m_model.reset();
    m_project = nullptr;
    m_cache   = nullptr;
}

void Preview::releaseGpu()
{
    drop();
    m_stale = true;
}

const EditPreview& Preview::edit() const
{
    return *m_project->preview();
}

void Preview::update()
{
    follow();
    for (size_t i = 0; i < m_leaves.size(); ++i)   // the cache syncs its own; the stroke is ours
        if (plan().leaves()[i].kind == PreviewLeaf::Kind::Stroke) m_leaves[i]->sync();
}

void Preview::follow()
{
    const EditPreview* e = m_project ? m_project->preview() : nullptr;
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
        case PreviewLeaf::Kind::Stroke: m_leaves.push_back(m_cache->make(std::make_unique<StrokeSource>(*m_project))); break;
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
