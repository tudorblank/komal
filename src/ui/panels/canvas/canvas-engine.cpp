#include "ui/panels/canvas/canvas-engine.hpp"
#include "document/workspace.hpp"
#include "gpu/device.hpp"

#include <algorithm>

bool CanvasEngine::init(const GpuDevice& gpu)
{
    m_ready = m_pipelines.create(GpuHandles{ gpu.device(), gpu.queue(), kTargetFormat });
    return m_ready;
}

void CanvasEngine::shutdown()
{
    while (!m_documents.empty()) close(m_documents.back()->id);
    m_pipelines.release();
    m_ready = false;
}

void CanvasEngine::open(DocumentId id, const Workspace& workspace)
{
    auto p = std::make_unique<Pictures>();
    p->id = id;
    p->workspace = &workspace;
    p->output = p->tiles.output(workspace.findPtr(workspace.master().id()));
    p->preview.init(workspace, p->tiles);
    makeThumbnails(*p);
    Pictures* raw = p.get();
    p->events = workspace.changed.connect([raw](const WorkspaceEvent& e) {
        if (e.type != WorkspaceEventType::NodeRemoved) return;
        raw->thumbnails.forget(e.node);
        raw->tiles.forget(e.node);
    });
    m_documents.push_back(std::move(p));
}

void CanvasEngine::close(DocumentId id)
{
    const auto it = std::find_if(m_documents.begin(), m_documents.end(), [&](const auto& p) { return p->id == id; });
    if (it == m_documents.end()) return;
    Pictures& p = **it;
    if (m_active == &p) m_active = nullptr;
    p.events.disconnect();
    p.thumbnails.shutdown();
    p.preview.shutdown();
    p.output.reset();
    p.tiles.release();
    m_documents.erase(it);
}

void CanvasEngine::activate(DocumentId id)
{
    for (const auto& p : m_documents)
        if (p->id == id) m_active = p.get();
}

void CanvasEngine::makeThumbnails(Pictures& p)
{
    p.thumbnails.init(m_ready ? &m_pipelines : nullptr, p.tiles, p.preview);
}

bool CanvasEngine::outOfMemory() const
{
    return std::any_of(m_documents.begin(), m_documents.end(), [](const auto& p) { return p->tiles.outOfMemory(); });
}

void CanvasEngine::releaseGpu()
{
    for (const auto& p : m_documents) {
        p->thumbnails.shutdown();
        p->preview.releaseGpu();   // its own tiles go into the pages: before they do
        p->tiles.releaseGpu();
    }
    m_pipelines.release();
    m_ready = false;
}

bool CanvasEngine::restoreGpu(const GpuDevice& gpu)
{
    m_ready = m_pipelines.create(GpuHandles{ gpu.device(), gpu.queue(), kTargetFormat });
    for (const auto& p : m_documents) makeThumbnails(*p);
    return m_ready;
}

void CanvasEngine::beginFrame()
{
    m_frameBudget = Deadline::in(kFrameBudget);
    if (!m_ready) return;
    for (const auto& p : m_documents) p->tiles.sync();   // before any view of them builds; the others' age out
    if (m_active) m_active->preview.update();             // ... or previews
}

void CanvasEngine::renderThumbnails(WGPUCommandEncoder enc)
{
    if (!m_active) return;
    m_active->thumbnails.refresh(*m_active->workspace, budget(Work::Thumbnails));   // the ones drawn this frame
    m_active->thumbnails.renderGpu(enc);
}

Deadline CanvasEngine::budget(Work work) const
{
    return Deadline::earlier(Deadline::in(kShare[size_t(work)]), m_frameBudget);
}
