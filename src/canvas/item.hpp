#pragma once
// CanvasItem: one thing on a canvas (CanvasScene), with a z order and visibility, and what a
// view hands an item while it renders (CanvasFrame, OverlayContext).
//
// An item takes part in up to three stages, each optional:
//   prepare()      CPU, every frame before rendering: upload changed data, catch up with its
//                  source (within the frame's shared time budget). Returns "I changed".
//   drawGpu()      wgpu, in world space, inside the view's canvas pass (z order): pixels that
//                  are part of the canvas picture (rasters, rectangles, the edit preview).
//   drawOverlay()  ImGui, in screen space, over the rendered canvas (z order): outlines,
//                  cursors, handles; always crisp, and changing them never re-renders.
// An item that only draws overlays says so (drawsOnGpu() false): showing, hiding or moving it
// then costs no re-render of the canvas.
//
// The items (canvas/items/):  checker (GPU)   rect (GPU)   raster (GPU)   preview (GPU)
//                             origin (GPU, two rects)
//                             outline (overlay)   tool-overlay (overlay, the tool's cursor)

#include "canvas/camera.hpp"
#include "canvas/pipelines.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

struct ImDrawList;
class CanvasScene;

// What a view hands to items while it renders a frame.
class CanvasFrame {
public:
    struct Target {
        WGPUCommandEncoder encoder;           // records before the view's own pass begins
        float    viewW, viewH;                // view size in points (the camera's units)
        uint32_t pixelW, pixelH;              // the view's target in pixels
    };
    CanvasFrame(const CanvasPipelines& p, const CanvasCamera& cam, ViewRect visible, Target t, double budgetMs)
        : pipelines(p), camera(cam), view(visible), target(t), m_deadline(Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                                                    std::chrono::duration<double, std::milli>(budgetMs))) {}

    const CanvasPipelines& pipelines;
    const CanvasCamera&    camera;
    const ViewRect         view;     // world rectangle on screen
    const Target           target;   // prepare() may record its own passes into target.encoder

    // CPU time left for prepare() work this frame, shared by every item.
    bool inBudget() const { return Clock::now() < m_deadline; }

    // The level of detail to draw at this zoom, 0..levels-1: level L halves level L-1, and the
    // level picked has texels covering 0.5..1 target pixels (0 when zoomed in).
    int lodLevel(int levels) const
    {
        const float pixelsPerPoint = target.viewW > 0.0f ? float(target.pixelW) / target.viewW : 1.0f;
        const float z = camera.zoom * pixelsPerPoint;   // target pixels per world pixel
        if (z >= 1.0f) return 0;
        return std::clamp(int(std::floor(std::log2(1.0f / z))), 0, levels - 1);
    }

    // Binds `pipeline` and the camera, unless they're bound already (consecutive items of a
    // kind share both).
    void bind(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline)
    {
        if (pipeline == m_bound) return;
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, camera.bindGroup(), 0, nullptr);
        m_bound = pipeline;
    }
    // Binds a pipeline that brings its own group 0 (no camera); the next bind() rebinds both.
    void bindOwn(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline)
    {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        m_bound = pipeline;
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point  m_deadline;
    WGPURenderPipeline m_bound = nullptr;
};

// What overlay painting gets: the view's camera and where it sits on screen.
struct OverlayContext {
    const CanvasCamera& camera;
    float originX, originY;   // screen position of the view's top-left corner
    float width, height;      // view size in points

    Vec2 toScreen(float wx, float wy) const
    {
        const Vec2 s = camera.worldToScreen({ wx, wy });
        return { originX + s.x, originY + s.y };
    }
};

class CanvasItem {
public:
    CanvasItem() = default;
    virtual ~CanvasItem() = default;
    CanvasItem(const CanvasItem&) = delete;
    CanvasItem& operator=(const CanvasItem&) = delete;

    CanvasScene* scene() const { return m_scene; }

    float z() const { return m_z; }
    void  setZ(float z);
    bool  visible() const { return m_visible; }
    void  setVisible(bool visible);

    // ---- what an item implements (all optional, see the top) ----
    virtual bool prepare(CanvasFrame&) { return false; }
    virtual void drawGpu(WGPURenderPassEncoder, CanvasFrame&) {}
    virtual void drawOverlay(ImDrawList*, const OverlayContext&) {}
    virtual void releaseGpu() {}   // drop GPU resources; prepare() may recreate them later
    // False for overlay-only items: showing, hiding or re-ordering them then costs no re-render.
    virtual bool drawsOnGpu() const { return true; }
    // False while what the item would draw on screen is half-way through an update (some
    // visible tiles new, some old): views keep their last complete picture meanwhile.
    virtual bool settled() const { return true; }

protected:
    void update();   // "I look different now": views re-render on their next frame

private:
    friend class CanvasScene;
    CanvasScene* m_scene = nullptr;
    float    m_z = 0.0f;
    uint64_t m_order = 0;   // insertion order: ties in z keep it
    bool     m_visible = true;
};
