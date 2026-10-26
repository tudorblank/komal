#pragma once
#include "gfx/handles.hpp"

struct Vec2 { float x = 0.0f, y = 0.0f; };

// World-space rectangle, e.g. what is on screen (for culling).
struct ViewRect {
    float minX, minY, maxX, maxY;
    bool overlaps(float x0, float y0, float x1, float y1) const { return x1 >= minX && x0 <= maxX && y1 >= minY && y0 <= maxY; }
};

// The canvas camera of one view: world (pixel) space -> viewport, plus the uniform that hands
// the resulting view-projection to the shaders (bind group 0 of every canvas pipeline).
//
//   screen = world * zoom + pan        (viewport-local points, origin top-left)
//
// `pan` is measured from the viewport's top-left corner, so resizing, undocking or
// redocking the panel never moves what is under that corner; the other edges reveal more
// or less of the canvas.
class CanvasCamera {
public:
    Vec2  pan  = { 0.0f, 0.0f };
    float zoom = 1.0f;

    static constexpr float kMinZoom = 0.05f;
    static constexpr float kMaxZoom = 64.0f;

    bool create(const GpuHandles& ctx, WGPUBindGroupLayout layout);   // layout: CanvasPipelines::cameraLayout
    void release();
    void upload(float viewW, float viewH) const;   // writes view-projection for a viewW x viewH viewport

    WGPUBindGroup bindGroup() const { return m_bindGroup; }

    Vec2 worldToScreen(Vec2 w) const { return { w.x * zoom + pan.x, w.y * zoom + pan.y }; }
    Vec2 screenToWorld(Vec2 s) const { return { (s.x - pan.x) / zoom, (s.y - pan.y) / zoom }; }
    ViewRect visibleWorld(float viewW, float viewH) const
    {
        const Vec2 a = screenToWorld({ 0.0f, 0.0f }), b = screenToWorld({ viewW, viewH });
        return { a.x, a.y, b.x, b.y };
    }

    // Zooms by `factor` keeping the world point under `screen` fixed. Returns false if clamped flat.
    bool zoomAt(Vec2 screen, float factor);
    // Fits the world rectangle (minX, minY)..(maxX, maxY) in a viewW x viewH view, centred, with
    // a margin. Too big for the smallest zoom: its middle is centred.
    void frame(float minX, float minY, float maxX, float maxY, float viewW, float viewH);
    // Puts world point (x, y) in the middle of the view, at the current zoom.
    void centerOn(float x, float y, float viewW, float viewH) { pan = { viewW * 0.5f - x * zoom, viewH * 0.5f - y * zoom }; }

private:
    GpuHandles    m_ctx;
    WGPUBuffer    m_buffer    = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
};
