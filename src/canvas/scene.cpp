#include "scene.hpp"

#include <algorithm>

bool CanvasScene::init(const CanvasPipelines& pipelines)
{
    m_pipelines = &pipelines;
    m_ready = bool(pipelines.atlasPipeline);
    return m_ready;
}

void CanvasScene::shutdown()
{
    for (auto& item : m_items) item->releaseGpu();
    m_items.clear();
    m_ready = false;
}

void CanvasScene::adopt(std::unique_ptr<CanvasItem> item)
{
    item->m_scene = this;
    item->m_order = m_nextOrder++;
    m_items.push_back(std::move(item));
    m_sortDirty = true;
    if (m_items.back()->drawsOnGpu()) touch();
}

void CanvasScene::sortIfNeeded()
{
    if (!m_sortDirty) return;
    std::sort(m_items.begin(), m_items.end(), [](const auto& a, const auto& b) {
        return a->m_z != b->m_z ? a->m_z < b->m_z : a->m_order < b->m_order;
    });
    m_sortDirty = false;
}

bool CanvasScene::prepare(CanvasFrame& frame)
{
    if (!m_ready) return false;
    sortIfNeeded();
    bool changed = false;
    for (auto& item : m_items)
        if (item->m_visible) changed |= item->prepare(frame);
    return changed;
}

bool CanvasScene::settled() const
{
    for (const auto& item : m_items)
        if (item->m_visible && !item->settled()) return false;
    return true;
}

void CanvasScene::drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame)
{
    if (!m_ready) return;
    sortIfNeeded();
    for (auto& item : m_items)
        if (item->m_visible) item->drawGpu(pass, frame);
}

void CanvasScene::drawOverlays(ImDrawList* dl, const OverlayContext& ctx)
{
    sortIfNeeded();
    for (auto& item : m_items)
        if (item->m_visible) item->drawOverlay(dl, ctx);
}
