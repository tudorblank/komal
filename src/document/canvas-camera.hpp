#pragma once
#include "base/math.hpp"

#include <algorithm>
#include <cmath>

// World-space rectangle, e.g. what is on screen (for culling).
struct ViewRect {
    float minX, minY, maxX, maxY;
    bool overlaps(float x0, float y0, float x1, float y1) const { return x1 >= minX && x0 <= maxX && y1 >= minY && y0 <= maxY; }
};

// Where a canvas looks: world (pixel) space -> viewport. Plain math. The project keeps the canvas's
// (Project::canvasCamera(), changed by the canvas.* actions); a view renders through a copy of it
// and hands viewProj() to the shaders (CanvasView, bind group 0 of every canvas pipeline).
//
//   screen = R(rotation) * world * zoom + pan        (viewport points, origin top-left)
//
// rotation: radians, clockwise, in [0, 2pi); at 0 it is exactly scale + translate (pixel-exact).
// pan is from the viewport's top-left, so resizing the panel never moves what's under that corner.
class CanvasCamera {
public:
    Vec2  pan  = { 0.0f, 0.0f };
    float zoom = 1.0f;
    float rotation = 0.0f;   // set through rotateAt(), which keeps it in range

    static constexpr float kMinZoom = 0.05f;
    static constexpr float kMaxZoom = 64.0f;

    // World -> clip space for a viewW x viewH viewport; `worldOffset` shifts the content first.
    Mat4 viewProj(float viewW, float viewH, Vec2 worldOffset = {}) const
    {
        const Vec2 o = rotated({ worldOffset.x * zoom, worldOffset.y * zoom });
        return Mat4::ortho(viewW, viewH) * Mat4::similarity(pan.x + o.x, pan.y + o.y, zoom, rotation);
    }

    Vec2 worldToScreen(Vec2 w) const
    {
        const Vec2 r = rotated({ w.x * zoom, w.y * zoom });
        return { r.x + pan.x, r.y + pan.y };
    }
    Vec2 screenToWorld(Vec2 s) const
    {
        const Vec2 r = rotated({ (s.x - pan.x) / zoom, (s.y - pan.y) / zoom }, -1.0f);
        return { r.x, r.y };
    }
    // The world rectangle around everything on screen (all of it, when the view is rotated).
    ViewRect visibleWorld(float viewW, float viewH) const
    {
        const Vec2 c[4] = { screenToWorld({ 0.0f, 0.0f }), screenToWorld({ viewW, 0.0f }),
                            screenToWorld({ 0.0f, viewH }), screenToWorld({ viewW, viewH }) };
        ViewRect r = { c[0].x, c[0].y, c[0].x, c[0].y };
        for (const Vec2& p : c) {
            r.minX = std::min(r.minX, p.x); r.maxX = std::max(r.maxX, p.x);
            r.minY = std::min(r.minY, p.y); r.maxY = std::max(r.maxY, p.y);
        }
        return r;
    }

    void panBy(Vec2 screenDelta) { pan.x += screenDelta.x; pan.y += screenDelta.y; }
    // Zooms by `factor` keeping the world point under `screen` fixed. Returns false if clamped flat.
    bool zoomAt(Vec2 screen, float factor);
    // Fits the world rectangle (minX, minY)..(maxX, maxY) in a viewW x viewH view, centred, with
    // a margin. Too big for the smallest zoom: its middle is centred.
    void frame(float minX, float minY, float maxX, float maxY, float viewW, float viewH);
    // Puts world point (x, y) in the middle of the view, at the current zoom and rotation.
    void centerOn(float x, float y, float viewW, float viewH)
    {
        const Vec2 r = rotated({ x * zoom, y * zoom });
        pan = { viewW * 0.5f - r.x, viewH * 0.5f - r.y };
    }
    // Rotates to `radians` (any value: wrapped into [0, 2pi)) keeping the world point under `screen` fixed.
    void rotateAt(Vec2 screen, float radians);
    static float wrapAngle(float radians);

private:
    // `v` rotated by the camera's rotation (sign -1: by its inverse).
    Vec2 rotated(Vec2 v, float sign = 1.0f) const
    {
        if (rotation == 0.0f) return v;
        const float c = std::cos(rotation), s = std::sin(rotation) * sign;
        return { v.x * c - v.y * s, v.x * s + v.y * c };
    }
};
