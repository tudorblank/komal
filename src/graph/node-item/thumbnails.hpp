#pragma once
// Each node's output fitted into 256x192 over a checkerboard, rendered on the GPU into slots of
// shared page textures, and kept up to date tile by tile like the canvas:
//
//   - the node reports which of its output tiles changed (Node::watchOutput); only those are
//     fetched again
//   - each is box-filtered straight into one coarse level (a texel is 2^L world pixels, L picked so
//     a texel covers at most a thumbnail pixel) and uploaded to tile pages all thumbnails share
//   - the slot is rendered from those tiles: the checkerboard, then the tiles, fitted
//
// Only thumbnails drawn this frame are updated, within a per-frame budget; a rebuild from scratch
// (the first, or the level changed) keeps the old picture until it's done.

#include "canvas/tiles/atlas.hpp"
#include "gfx/handles.hpp"
#include "node/node.hpp"
#include "raster/tile-grid.hpp"

#include <imgui.h>
#include <webgpu/webgpu.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <chrono>
#include <memory>
#include <vector>

class Project;

class NodeItemThumbnails {
public:
    static constexpr int kWidth  = 256;
    static constexpr int kHeight = 192;

    struct View {
        ImTextureID texture = ImTextureID_Invalid;
        ImVec2      uv0, uv1;     // the picture, in its slot
        ImVec2      fit0, fit1;   // where it sits in the thumbnail's rect, 0..1 (proportions kept, centred)
        bool        empty = true;   // the node outputs nothing
    };

    void init(const CanvasPipelines* pipelines);   // the canvas's; null: no thumbnails
    void shutdown();

    // The last picture of `id` (false until there is one); also asks to keep it fresh.
    bool get(NodeId id, View& out);
    void forget(NodeId id);
    void refresh(Project& project, double budgetMs);   // once per frame, after drawing: tiles and draws
    void renderGpu(WGPUCommandEncoder enc);            // the slots refresh() changed

    double nextFrameIn() const { return m_nextFrameIn; }   // source tiles left over: now (Application::nextFrameIn)

private:
    using Clock = std::chrono::steady_clock;

    static constexpr int kMaxLevel = 6;   // a texel never spans more than one source tile
    static constexpr int kPageSize = 2048;
    static constexpr int kCols     = kPageSize / kWidth;
    static constexpr int kRows     = kPageSize / kHeight;
    static constexpr int kSlots    = kCols * kRows;

    struct Page {
        WGPUTexture     texture = nullptr;
        WGPUTextureView view    = nullptr;
        std::vector<int> freeSlots;
    };
    struct Thumb {
        std::shared_ptr<TileWatch> watch;   // the node's output changes
        uint64_t     revision = ~0ull;      // the node's, when last fitted
        BoundsI      bounds;                // what the picture is fitted to
        int          level = -1;
        TileGrid     tiles;                 // the level's tiles (what the atlas holds)
        TileAtlas    atlas;                 // ... on the GPU, in the shared tile pages
        TileSet      pending;               // source tiles still to fold in
        bool         rebuilding = false;    // pending is everything: the old picture stays meanwhile
        TileDrawList draw;
        GpuUniform   camera, checker;
        int  page = -1, slot = -1;
        int  fitX = 0, fitY = 0, fitW = 0, fitH = 0;   // the picture in its slot, pixels
        bool stale = true;    // the slot needs rendering
        bool shown = false;   // a picture is in the slot (or there's nothing to show)
        bool empty = true;
    };

    void update(Node& n, Thumb& t, Clock::time_point deadline);
    void fold(Node& n, Thumb& t, TileKey key, TileSet& touched);   // one source tile into the level
    bool prepareRender(Thumb& t);   // slot, uniforms, draw list
    bool allocate(Thumb& t);
    void release(Thumb& t);

    const CanvasPipelines* m_pipelines = nullptr;
    AtlasPages m_tilePages;   // every thumbnail's tiles; before the thumbnails, whose atlases free into it
    std::vector<Page> m_pages;
    absl::flat_hash_map<NodeId, std::unique_ptr<Thumb>> m_thumbs;
    absl::flat_hash_set<NodeId> m_requested;   // drawn this frame
    std::vector<NodeId> m_toRender;            // by refresh(), for renderGpu()
    double m_nextFrameIn = 0.0;   // set by refresh()
    RenderCtx m_render;
};
