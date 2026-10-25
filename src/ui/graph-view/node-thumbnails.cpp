#include "node-thumbnails.h"
#include "core/perf.h"
#include "core/project.h"
#include "gfx/gfx-context.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

constexpr int kSub     = 2;    // samples per thumbnail pixel, per axis
constexpr int kChecker = 8;    // checkerboard square, thumbnail pixels
constexpr uint8_t kCheckerLight = 72, kCheckerDark = 52;

} // namespace

void NodeThumbnails::init(WGPUDevice device, WGPUQueue queue)
{
    m_device = device;
    m_queue  = queue;
}

void NodeThumbnails::shutdown()
{
    for (Page& p : m_pages) {
        if (p.view)    wgpuTextureViewRelease(p.view);
        if (p.texture) wgpuTextureRelease(p.texture);
    }
    m_pages.clear();
    m_thumbs.clear();
    m_requested.clear();
}

bool NodeThumbnails::get(NodeId id, View& out)
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

void NodeThumbnails::forget(NodeId id)
{
    auto it = m_thumbs.find(id);
    if (it == m_thumbs.end()) return;
    if (it->second.page >= 0) m_pages[it->second.page].freeSlots.push_back(it->second.slot);
    m_thumbs.erase(it);
}

bool NodeThumbnails::allocate(Thumb& t)
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

void NodeThumbnails::refresh(Project& project, double budgetMs)
{
    PerfScope perf(Perf::Thumbnails);
    if (project.strokeActive()) {   // brought up to date once the stroke ends
        m_requested.clear();
        return;
    }
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(budgetMs));
    const double now = ImGui::GetTime();

    for (NodeId id : m_requested) {
        if (Clock::now() >= deadline) break;   // the rest stay requested by being drawn again
        Node* n = project.find(id);
        if (!n) continue;
        Thumb& t = m_thumbs[id];
        if (t.rendered && t.revision == n->revision()) continue;
        if (t.rendered && now - t.renderedAt < kMinInterval) continue;
        if (t.page < 0 && !allocate(t)) continue;
        render(*n, t);
        t.revision   = n->revision();
        t.renderedAt = now;
        t.rendered   = true;
    }
    m_requested.clear();
}

void NodeThumbnails::render(Node& n, Thumb& t)
{
    m_pixels.assign(size_t(kWidth) * kHeight, RGBA{ 0, 0, 0, 0 });
    const BoundsI b = n.bounds();
    t.empty = b.empty();

    if (!t.empty) {
        // Fit the output's bounds, keeping its aspect.
        const float bw = float(b.maxX - b.minX + 1), bh = float(b.maxY - b.minY + 1);
        const float s  = std::max(bw / kWidth, bh / kHeight);   // world pixels per thumbnail pixel
        const int fw = std::clamp(int(std::lround(bw / s)), 1, kWidth);
        const int fh = std::clamp(int(std::lround(bh / s)), 1, kHeight);
        const int ox = (kWidth - fw) / 2, oy = (kHeight - fh) / 2;

        // Sample positions along each axis (monotonic, so every tile owns a contiguous range).
        std::vector<int> xs(size_t(fw) * kSub), ys(size_t(fh) * kSub);
        for (size_t i = 0; i < xs.size(); ++i) xs[i] = b.minX + int(std::floor((float(i) + 0.5f) * s / kSub));
        for (size_t i = 0; i < ys.size(); ++i) ys[i] = b.minY + int(std::floor((float(i) + 0.5f) * s / kSub));

        m_accum.assign(size_t(fw) * fh * 4, 0);
        const auto range = [](const std::vector<int>& v, int lo, int hi) {
            return std::make_pair(std::lower_bound(v.begin(), v.end(), lo) - v.begin(),
                                  std::upper_bound(v.begin(), v.end(), hi) - v.begin());
        };
        // Only tiles that exist and hold a sample are rendered: work follows the content and
        // the thumbnail size, not the size of the bounds.
        for (TileKey key : n.footprint()) {
            const auto [tx, ty] = TileUtils::unpackKey(key);
            const int wx = TileUtils::tileToWorld(tx), wy = TileUtils::tileToWorld(ty);
            const auto [x0, x1] = range(xs, wx, wx + Tile::kSize - 1);
            const auto [y0, y1] = range(ys, wy, wy + Tile::kSize - 1);
            if (x0 >= x1 || y0 >= y1) continue;
            const Tile* tile = renderTile(n, tx, ty, m_render);
            if (!tile) continue;
            for (auto sy = y0; sy < y1; ++sy) {
                uint32_t* row = m_accum.data() + size_t(sy / kSub) * fw * 4;
                for (auto sx = x0; sx < x1; ++sx) {
                    const RGBA p = tile->getReadPx(xs[sx] - wx, ys[sy] - wy);
                    uint32_t* a = row + size_t(sx / kSub) * 4;
                    a[0] += p.r; a[1] += p.g; a[2] += p.b; a[3] += p.a;
                }
            }
        }

        // Premultiplied average over a checkerboard: opaque, straight = premultiplied.
        constexpr uint32_t kDiv = kSub * kSub;
        for (int y = 0; y < fh; ++y)
            for (int x = 0; x < fw; ++x) {
                const uint32_t* a = m_accum.data() + (size_t(y) * fw + x) * 4;
                const uint32_t alpha = a[3] / kDiv;
                const uint32_t bg = ((x / kChecker + y / kChecker) & 1) ? kCheckerLight : kCheckerDark;
                const uint32_t under = bg * (255 - alpha) / 255;
                m_pixels[size_t(oy + y) * kWidth + ox + x] = {
                    uint8_t(std::min<uint32_t>(255, a[0] / kDiv + under)),
                    uint8_t(std::min<uint32_t>(255, a[1] / kDiv + under)),
                    uint8_t(std::min<uint32_t>(255, a[2] / kDiv + under)), 255 };
            }
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
