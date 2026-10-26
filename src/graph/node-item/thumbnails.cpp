#include "graph/node-item/thumbnails.hpp"
#include "core/perf.hpp"
#include "core/project.hpp"
#include "gfx/handles.hpp"
#include "node/node-picture.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

void NodeItemThumbnails::init(WGPUDevice device, WGPUQueue queue)
{
    m_device = device;
    m_queue  = queue;
}

void NodeItemThumbnails::shutdown()
{
    for (Page& p : m_pages) {
        if (p.view)    wgpuTextureViewRelease(p.view);
        if (p.texture) wgpuTextureRelease(p.texture);
    }
    m_pages.clear();
    m_thumbs.clear();
    m_requested.clear();
}

bool NodeItemThumbnails::get(NodeId id, View& out)
{
    m_requested.insert(id);
    auto it = m_thumbs.find(id);
    if (it == m_thumbs.end() || !it->second.rendered) return false;
    const Thumb& t = it->second;
    const float u = float((t.slot % kCols) * kWidth) / kPageSize;
    const float v = float((t.slot / kCols) * kHeight) / kPageSize;
    out.texture = (ImTextureID)(uintptr_t)m_pages[t.page].view;
    out.uv0     = ImVec2(u, v);
    out.uv1     = ImVec2(u + float(kWidth) / kPageSize, v + float(kHeight) / kPageSize);
    out.empty   = t.empty;
    return true;
}

void NodeItemThumbnails::forget(NodeId id)
{
    auto it = m_thumbs.find(id);
    if (it == m_thumbs.end()) return;
    if (it->second.page >= 0) m_pages[it->second.page].freeSlots.push_back(it->second.slot);
    m_thumbs.erase(it);
}

bool NodeItemThumbnails::allocate(Thumb& t)
{
    auto page = std::find_if(m_pages.begin(), m_pages.end(), [](const Page& p) { return !p.freeSlots.empty(); });
    if (page == m_pages.end()) {
        Page p;
        WGPUTextureDescriptor td{};
        td.label         = sv("Node Thumbnails");
        td.dimension     = WGPUTextureDimension_2D;
        td.size          = { (uint32_t)kPageSize, (uint32_t)kPageSize, 1 };
        td.format        = WGPUTextureFormat_RGBA8Unorm;
        td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        p.texture = wgpuDeviceCreateTexture(m_device, &td);
        if (!p.texture) return false;
        p.view = wgpuTextureCreateView(p.texture, nullptr);
        for (int i = kSlots; i-- > 0;) p.freeSlots.push_back(i);   // popped from the back: slot 0 first
        m_pages.push_back(std::move(p));
        page = m_pages.end() - 1;
    }
    t.page = int(page - m_pages.begin());
    t.slot = page->freeSlots.back();
    page->freeSlots.pop_back();
    return true;
}

void NodeItemThumbnails::refresh(Project& project, double budgetMs)
{
    PerfScope perf(Perf::Thumbnails);
    m_pending = false;
    if (project.strokeActive()) {   // brought up to date once the stroke ends
        m_requested.clear();
        return;
    }
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(budgetMs));
    const double now = ImGui::GetTime();

    for (NodeId id : m_requested) {
        if (Clock::now() >= deadline) { m_pending = true; break; }   // the rest stay requested by being drawn again
        Node* n = project.find(id);
        if (!n) continue;
        Thumb& t = m_thumbs[id];
        if (t.rendered && t.revision == n->revision()) continue;
        if (t.rendered && now - t.renderedAt < kMinInterval) { m_pending = true; continue; }
        if (t.page < 0 && !allocate(t)) continue;
        render(*n, t);
        t.revision   = n->revision();
        t.renderedAt = now;
        t.rendered   = true;
    }
    m_requested.clear();
}

void NodeItemThumbnails::render(Node& n, Thumb& t)
{
    m_pixels.assign(size_t(kWidth) * kHeight, RGBA{ 0, 0, 0, 0 });
    const BoundsI b = n.bounds();
    t.empty = b.empty();
    if (!t.empty) {   // fitted, aspect kept, centred
        const float s = std::max(float(b.width()) / kWidth, float(b.height()) / kHeight);
        const int fw = std::clamp(int(std::lround(b.width() / s)), 1, kWidth);
        const int fh = std::clamp(int(std::lround(b.height() / s)), 1, kHeight);
        renderNodePicture(n, b, fw, fh, &m_pixels[size_t((kHeight - fh) / 2) * kWidth + (kWidth - fw) / 2], kWidth, m_render);
    }

    WGPUTexelCopyTextureInfo dst{};
    dst.texture = m_pages[t.page].texture;
    dst.origin  = { uint32_t((t.slot % kCols) * kWidth), uint32_t((t.slot / kCols) * kHeight), 0 };
    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow  = kWidth * sizeof(RGBA);
    layout.rowsPerImage = kHeight;
    const WGPUExtent3D size = { uint32_t(kWidth), uint32_t(kHeight), 1 };
    wgpuQueueWriteTexture(m_queue, &dst, m_pixels.data(), m_pixels.size() * sizeof(RGBA), &layout, &size);
}
