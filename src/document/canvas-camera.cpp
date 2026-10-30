#include "document/canvas-camera.hpp"
#include "base/math.hpp"
#include <algorithm>
#include <cmath>

bool CanvasCamera::zoomAt(Vec2 screen, float factor)
{
    const float newZoom = std::clamp(zoom * factor, kMinZoom, kMaxZoom);
    if (newZoom == zoom) return false;
    const float k = newZoom / zoom;
    pan.x = screen.x + (pan.x - screen.x) * k;
    pan.y = screen.y + (pan.y - screen.y) * k;
    zoom  = newZoom;
    return true;
}

void CanvasCamera::frame(float minX, float minY, float maxX, float maxY, float viewW, float viewH)
{
    if (viewW <= 0.0f || viewH <= 0.0f) return;
    constexpr float kMargin = 0.9f;   // of the view the rectangle fills
    // The rectangle as it stands on screen: rotated, it needs its bounding box's room.
    const float c = std::abs(std::cos(rotation)), s = std::abs(std::sin(rotation));
    const float rw = std::max(maxX - minX, 1.0f), rh = std::max(maxY - minY, 1.0f);
    const float w = rw * c + rh * s, h = rw * s + rh * c;
    zoom = std::clamp(std::min(viewW / w, viewH / h) * kMargin, kMinZoom, kMaxZoom);
    centerOn((minX + maxX) * 0.5f, (minY + maxY) * 0.5f, viewW, viewH);
}

float CanvasCamera::wrapAngle(float radians)
{
    float r = std::fmod(radians, kTau);
    if (r < 0.0f) r += kTau;
    return r >= kTau ? 0.0f : r;   // 360 is 0
}

void CanvasCamera::rotateAt(Vec2 screen, float radians)
{
    const Vec2 w = screenToWorld(screen);
    rotation = wrapAngle(radians);
    const Vec2 r = rotated({ w.x * zoom, w.y * zoom });
    pan = { screen.x - r.x, screen.y - r.y };
}
