#pragma once
// The canvas engine, made once and used by every view of it: the pipelines all canvas drawing
// uses, and each open document's pictures: every node's pixels on the GPU (TileCache: the
// master's output for the canvas and the navigator, each node's for its thumbnail, each built
// once), the node thumbnails, and the edit in progress as the GPU shows it (Preview). Views show
// the active document's; the others keep theirs (their tiles age out of the cache as usual) until
// they close.
//
//   engine.open(id, workspace) / close(id) / activate(id)   // as the documents change (UserInterface)
//   ...                              // the panels draw (asking for thumbnails, editing the workspace)
//   engine.beginFrame();             // output changes -> tiles, the preview follows; the frame's budget starts
//   ...                              // the views render (the canvas panel, the navigator), each on its budget
//   engine.renderThumbnails(encoder);   // last: the thumbnails asked for, on what's left
//
// The CPU time a frame spends building pictures (tiles, previews) comes from one place: each kind
// of work has its share, cut to what's left of the frame's whole, asked for in priority order.

#include "base/signal.hpp"
#include "document/document.hpp"
#include "ui/panels/canvas/node-thumbnails.hpp"
#include "ui/panels/canvas/pipelines.hpp"
#include "ui/panels/canvas/preview/preview.hpp"
#include "ui/panels/canvas/tiles/tile-cache.hpp"

#include <memory>
#include <vector>

class GpuDevice;
class Workspace;

class CanvasEngine {
public:
    static constexpr WGPUTextureFormat kTargetFormat = WGPUTextureFormat_RGBA8Unorm;   // every canvas view's

    bool init(const GpuDevice& gpu);   // false: no pipelines (nothing canvas draws)
    void shutdown();
    // A document's pictures: made as it opens, gone as it closes (before its workspace goes); the
    // active one's are the ones below.
    void open(DocumentId id, const Workspace& workspace);
    void close(DocumentId id);
    void activate(DocumentId id);
    // The GPU device is made again (it was lost): everything on it let go of, then made on the new
    // one. The tiles' sources stay, and build again as they're shown.
    void releaseGpu();
    bool restoreGpu(const GpuDevice& gpu);
    bool ready() const { return m_ready; }

    void beginFrame();                               // once a frame: after the panels drew, before any view renders
    void renderThumbnails(WGPUCommandEncoder enc);   // once a frame: after the views rendered
    Clock::Seconds nextFrameIn() const { return m_active ? m_active->thumbnails.nextFrameIn() : kNoFrame; }   // base/frames.hpp

    enum class Work : uint8_t { Canvas, Navigator, Thumbnails };   // in the order they render
    Deadline budget(Work work) const;   // its share of this frame, at most what's left of it

    const CanvasPipelines& pipelines() const { return m_pipelines; }
    bool outOfMemory() const;   // some of a picture has no room on the GPU
    // The active document's (there always is one once the interface is up).
    const TileCache& tiles() const { return m_active->tiles; }
    const Preview&  preview() const { return m_active->preview; }
    const std::shared_ptr<RasterTiles>& outputTiles() const { return m_active->output; }   // the master's
    NodeThumbnails& thumbnails() const { return m_active->thumbnails; }   // drawing one asks for it

private:
    struct Pictures {   // one document's
        DocumentId       id;
        const Workspace* workspace;
        TileCache        tiles;
        std::shared_ptr<RasterTiles> output;   // the master's
        Preview          preview;
        NodeThumbnails   thumbnails;
        Connection       events;   // workspace.changed: a removed node's thumbnail and tiles go with it
    };
    void makeThumbnails(Pictures& p);

    static constexpr Clock::Milliseconds kFrameBudget{ 6.0 };   // the whole: leaves room for the rest of the frame
    static constexpr Clock::Milliseconds kShare[] = { Clock::Milliseconds(4.0), Clock::Milliseconds(2.0), Clock::Milliseconds(3.0) };   // per Work

    CanvasPipelines m_pipelines;       // before what draws with it
    std::vector<std::unique_ptr<Pictures>> m_documents;
    Pictures*       m_active = nullptr;
    Deadline        m_frameBudget;   // this frame's whole, from beginFrame()
    bool m_ready = false;
};
