#pragma once
// CanvasItem: one thing on a canvas (CanvasScene), with a z order and visibility, plus what a
// view hands an item while it renders (CanvasFrame, OverlayContext).
//
// An item takes part in up to three stages, each optional:
//   prepare()      CPU, before rendering: upload changed data, catch up with its source within
//                  the frame's shared budget. Returns "I changed".
//   drawGpu()      in the view's canvas pass, world space, by z: part of the picture.
//   drawOverlay()  ImGui, screen space, over the canvas: outlines. Never re-renders.
// Overlay-only items return false from drawsOnGpu(): showing or moving them costs no re-render.

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
        WGPUCommandEncoder encoder;   // records before the view's own pass begins
        float    viewW, viewH;        // view size in points (the camera's units)
        uint32_t pixelW, pixelH;      // the view's target in pixels
    };
    CanvasFrame(const CanvasPipelines& p, const CanvasCamera& cam, ViewRect visible, Target t, double budgetMs)
        : pipelines(p), camera(cam), view(visible), target(t),
          m_deadline(Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(budgetMs))) {}

    const CanvasPipelines& pipelines;
    const CanvasCamera&    camera;
    const ViewRect         view;     // world rectangle around what's on screen
    const Target           target;   // prepare() may record its own passes into target.encoder

    // CPU time left for prepare() work this frame, shared by every item.
    bool inBudget() const { return Clock::now() < m_deadline; }

    float pixelsPerPoint() const { return target.viewW > 0.0f ? float(target.pixelW) / target.viewW : 1.0f; }

    // Level of detail for this zoom, 0..levels-1: level L halves L-1, picked so a texel covers
    // 0.5..1 target pixels (0 when zoomed in).
    int lodLevel(int levels) const
    {
        const float z = camera.zoom * pixelsPerPoint();   // target pixels per world pixel
        if (z >= 1.0f) return 0;
        return std::clamp(int(std::floor(std::log2(1.0f / z))), 0, levels - 1);
    }

    // Binds `pipeline` and the camera, unless already bound.
    void bind(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline)
    {
        if (pipeline == m_bound) return;
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, camera.bindGroup(), 0, nullptr);
        m_bound = pipeline;
    }
    // Binds a pipeline that brings its own group 0 (no camera).
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
    float originX, originY;   // screen position of the view's top-left
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

    // ---- what an item implements (all optional)
    virtual bool prepare(CanvasFrame&) { return false; }
    virtual void drawGpu(WGPURenderPassEncoder, CanvasFrame&) {}
    virtual void drawOverlay(ImDrawList*, const OverlayContext&) {}
    virtual void releaseGpu() {}   // prepare() may recreate them later
    virtual bool drawsOnGpu() const { return true; }
    // False while its on-screen picture is half-updated: views keep their last complete one.
    virtual bool settled() const { return true; }

protected:
    void update();   // "I look different now": views re-render next frame

private:
    friend class CanvasScene;
    CanvasScene* m_scene = nullptr;
    float    m_z = 0.0f;
    uint64_t m_order = 0;   // insertion order breaks ties in z
    bool     m_visible = true;
};
