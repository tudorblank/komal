#pragma once
// Each node's output fitted into 256x192 over a checkerboard, rendered on the GPU into slots of
// shared page textures. The pixels are the node's output tiles from the engine's TileCache, at
// the one level whose texel covers at most a thumbnail pixel: shared with every other view of
// that node, and updated tile by tile as it changes.
//
// Only thumbnails drawn this frame are updated, within a per-frame budget. A slot is rendered
// again once every tile of its level is current: until then it keeps its old picture.
//
// While an edit in progress reaches a node, its slot shows the preview's picture instead
// (PreviewPicture, the preview's thumbnail branch), and the node's tiles wait: once the edit is
// over they catch up, and the slot keeps the preview's last picture until they have.

#include "canvas/preview/preview-picture.hpp"
#include "canvas/tiles/tile-cache.hpp"
#include "gfx/slot-pages.hpp"
#include "gfx/handles.hpp"

#include <imgui.h>
#include <webgpu/webgpu.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <memory>
#include <vector>

class Preview;
class Project;

class NodeThumbnails {
public:
    static constexpr int kWidth  = 256;
    static constexpr int kHeight = 192;

    struct View {
        ImTextureID texture = ImTextureID_Invalid;
        ImVec2      uv0, uv1;     // the picture, in its slot
        ImVec2      fit0, fit1;   // where it sits in the thumbnail's rect, 0..1 (proportions kept, centred)
        bool        empty = true;   // the node outputs nothing
    };

    void init(const CanvasPipelines* pipelines, TileCache& tiles, const Preview& preview);   // the engine's; null pipelines: no thumbnails
    void shutdown();

    // The last picture of `id` (false until there is one); also asks to keep it fresh.
    bool get(NodeId id, View& out);
    void forget(NodeId id);
    void refresh(const Project& project, const Deadline& budget);   // once a frame, after the views: tiles and draws (CanvasEngine)
    void renderGpu(WGPUCommandEncoder enc);            // the slots refresh() changed, and the previewed ones

    double nextFrameIn() const { return m_nextFrameIn; }   // tiles left to build: now (base/frames.hpp)

private:
    static constexpr int kPageSize = 2048;
    struct Thumb {
        std::shared_ptr<RasterTiles> tiles;   // the node's output
        BoundsI      bounds;                  // what the picture is fitted to
        int          level = -1;
        uint64_t     seen = 0;                // the level's revision, as last rendered
        TileDrawList draw;
        GpuUniform   camera, checker;
        SlotPages::Slot slot;                 // its picture, in m_pages
        int  fitX = 0, fitY = 0, fitW = 0, fitH = 0;   // the picture in its slot, pixels
        bool stale = true;    // fitted anew, or it showed a preview: the slot needs rendering
        bool shown = false;   // a picture is in the slot (or there's nothing to show)
        bool empty = true;
    };

    bool update(const Node& n, Thumb& t, const Deadline& budget);   // true: every tile current
    bool prepareRender(Thumb& t);   // slot, uniforms, draw list
    void renderPreview(WGPUCommandEncoder enc, NodeId id, Thumb& t);   // the preview's picture into its slot
    void release(Thumb& t);

    const CanvasPipelines* m_pipelines = nullptr;
    TileCache*     m_tiles   = nullptr;
    const Preview* m_preview = nullptr;
    absl::flat_hash_map<NodeId, std::unique_ptr<PreviewPicture>> m_pictures;   // of the nodes the edit reaches
    std::vector<NodeId> m_toPreview;           // by refresh(), for renderGpu()
    Deadline m_budget;                         // this frame's, from refresh()
    SlotPages  m_pages;   // every thumbnail's picture, rendered into its slot
    absl::flat_hash_map<NodeId, std::unique_ptr<Thumb>> m_thumbs;
    absl::flat_hash_set<NodeId> m_requested;   // drawn this frame
    std::vector<NodeId> m_toRender;            // by refresh(), for renderGpu()
    double m_nextFrameIn = 0.0;   // set by refresh()
};
