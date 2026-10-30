#include "canvas/canvas-engine.hpp"
#include "document/project.hpp"
#include "gfx/device.hpp"

bool CanvasEngine::init(const GpuDevice& gpu, const Project& project)
{
    m_project = &project;
    m_ready = m_pipelines.create(GpuHandles{ gpu.device(), gpu.queue(), kTargetFormat });
    m_output = m_tiles.output(project.findPtr(project.master().id()));
    m_preview.init(project, m_tiles);
    m_thumbnails.init(m_ready ? &m_pipelines : nullptr, m_tiles, m_preview);
    m_projectEvents = project.changed.connect([this](const ProjectEvent& e) {
        if (e.type != ProjectEventType::NodeRemoved) return;
        m_thumbnails.forget(e.node);
        m_tiles.forget(e.node);
    });
    return m_ready;
}

void CanvasEngine::shutdown()
{
    m_projectEvents.disconnect();
    m_thumbnails.shutdown();
    m_preview.shutdown();
    m_output.reset();
    m_tiles.release();
    m_pipelines.release();
    m_project = nullptr;
    m_ready = false;
}

void CanvasEngine::beginFrame()
{
    if (!m_project) return;
    m_frameBudget = Deadline::in(kFrameBudgetMs);
    if (m_ready) {
        m_tiles.sync();       // before any view of them builds
        m_preview.update();   // ... or previews
    }
}

void CanvasEngine::renderThumbnails(WGPUCommandEncoder enc)
{
    if (!m_project) return;
    m_thumbnails.refresh(*m_project, budget(Work::Thumbnails));   // the ones drawn this frame
    m_thumbnails.renderGpu(enc);
}

Deadline CanvasEngine::budget(Work work) const
{
    return Deadline::earlier(Deadline::in(kShareMs[size_t(work)]), m_frameBudget);
}
