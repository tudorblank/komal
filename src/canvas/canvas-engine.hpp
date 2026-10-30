#pragma once
// The canvas engine's shared state, made once and used by every view of it: the pipelines all
// canvas drawing uses, every node's pixels on the GPU (TileCache: the master's output for the
// canvas and the navigator, each node's for its thumbnail, each built once), and the node
// thumbnails; and the edit in progress as the GPU shows it (Preview), which every view previews.
//
//   ...                              // the panels draw (asking for thumbnails, editing the project)
//   engine.beginFrame();             // output changes -> tiles, the preview follows; the frame's budget starts
//   ...                              // the views render (the canvas panel, the navigator), each on its budget
//   engine.renderThumbnails(encoder);   // last: the thumbnails asked for, on what's left
//
// The CPU time a frame spends building pictures (tiles, previews) comes from one place: each kind
// of work has its share, cut to what's left of the frame's whole, asked for in priority order.

#include "base/signal.hpp"
#include "canvas/node-thumbnails.hpp"
#include "canvas/pipelines.hpp"
#include "canvas/preview/preview.hpp"
#include "canvas/tiles/tile-cache.hpp"

class GpuDevice;
class Project;

class CanvasEngine {
public:
    static constexpr WGPUTextureFormat kTargetFormat = WGPUTextureFormat_RGBA8Unorm;   // every canvas view's

    bool init(const GpuDevice& gpu, const Project& project);   // false: no pipelines (nothing canvas draws)
    void shutdown();
    bool ready() const { return m_ready; }

    void beginFrame();                               // once a frame: after the panels drew, before any view renders
    void renderThumbnails(WGPUCommandEncoder enc);   // once a frame: after the views rendered
    double nextFrameIn() const { return m_thumbnails.nextFrameIn(); }   // base/frames.hpp

    enum class Work : uint8_t { Canvas, Navigator, Thumbnails };   // in the order they render
    Deadline budget(Work work) const;   // its share of this frame, at most what's left of it

    const CanvasPipelines& pipelines() const { return m_pipelines; }
    const TileCache& tiles() const { return m_tiles; }
    const Preview&  preview() const { return m_preview; }
    const std::shared_ptr<RasterTiles>& outputTiles() const { return m_output; }   // the master's
    NodeThumbnails& thumbnails()  { return m_thumbnails; }

private:
    static constexpr double kFrameBudgetMs = 6.0;   // the whole: leaves room for the rest of the frame
    static constexpr double kShareMs[] = { 4.0, 2.0, 3.0 };   // per Work


    const Project*  m_project = nullptr;
    Connection      m_projectEvents;   // project.changed: a removed node's thumbnail goes with it
    CanvasPipelines m_pipelines;       // before what draws with it
    TileCache       m_tiles;
    std::shared_ptr<RasterTiles> m_output;   // the master's
    Preview         m_preview;
    NodeThumbnails  m_thumbnails;
    Deadline        m_frameBudget;   // this frame's whole, from beginFrame()
    bool m_ready = false;
};
