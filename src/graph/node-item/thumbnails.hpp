#pragma once
// Node thumbnails: each node's output, fitted into 256x192 and composited over a checkerboard,
// kept in slots of a few atlas textures (one texture for many nodes, never re-created, so
// ImGui's per-texture bind groups stay valid).
//
// Only thumbnails that were asked for (drawn) this frame are refreshed, and only when the
// node's output may have changed (its revision), at most every kMinInterval per node while it
// keeps changing, within a per-frame time budget. Nothing is refreshed during a brush stroke:
// the thumbnails catch up when it ends.

#include "node/node.hpp"

#include <imgui.h>
#include <webgpu/webgpu.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <vector>

class Project;

class NodeItemThumbnails {
public:
    static constexpr int kWidth  = 256;
    static constexpr int kHeight = 192;

    struct View {
        ImTextureID texture = ImTextureID_Invalid;
        ImVec2      uv0, uv1;
        bool        empty = true;   // the node outputs nothing
    };

    void init(WGPUDevice device, WGPUQueue queue);
    void shutdown();

    // The last rendered thumbnail of `id`, and a request to keep it fresh. False until the
    // first render.
    bool get(NodeId id, View& out);
    void forget(NodeId id);
    void refresh(Project& project, double budgetMs);   // once per frame, after drawing

    bool pending() const { return m_pending; }   // stale thumbnails left for later frames

private:
    static constexpr int    kPageSize    = 2048;
    static constexpr int    kCols        = kPageSize / kWidth;
    static constexpr int    kRows        = kPageSize / kHeight;
    static constexpr int    kSlots       = kCols * kRows;
    static constexpr double kMinInterval = 1.0 / 12.0;   // seconds between renders of a changing node

    struct Page {
        WGPUTexture     texture = nullptr;
        WGPUTextureView view    = nullptr;
        std::vector<int> freeSlots;
    };
    struct Thumb {
        int      page = -1, slot = -1;
        uint64_t revision   = ~0ull;
        double   renderedAt = -1.0;
        bool     rendered   = false;
        bool     empty      = true;
    };

    bool allocate(Thumb& t);
    void render(Node& n, Thumb& t);

    WGPUDevice m_device = nullptr;
    WGPUQueue  m_queue  = nullptr;
    std::vector<Page> m_pages;
    bool m_pending = false;
    absl::flat_hash_map<NodeId, Thumb> m_thumbs;
    absl::flat_hash_set<NodeId> m_requested;   // drawn this frame
    RenderCtx m_render;
    std::vector<RGBA>     m_pixels;
    std::vector<uint32_t> m_accum;
};
