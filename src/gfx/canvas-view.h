#pragma once
#include "gfx/canvas-scene.h"
#include "gfx/offscreen-target.h"

#include <chrono>

// A camera and an offscreen target looking at a scene (the QGraphicsView part). Several views
// can show one scene; each has its own camera.
//
// render() lets the scene's items catch up, then redraws the target only if something
// visible changed: an item, the camera, or the size. Nothing has to be flagged by hand.
// While an item is half-way through an update (not settled) the target keeps its last
// complete picture, so a change appears at once instead of tile by tile; only moving the
// camera, or an update taking longer than kMaxHoldSeconds, shows it in progress.
class CanvasView {
public:
    bool init(CanvasScene& scene);   // after scene.init()
    void shutdown();

    CanvasScene&  scene()  { return *m_scene; }
    CanvasCamera& camera() { return m_camera; }
    const CanvasCamera& camera() const { return m_camera; }

    // `width` x `height` in points (the camera's units); the target gets device pixels.
    bool resize(float width, float height, float pixelScaleX, float pixelScaleY);
    float width()  const { return m_width; }
    float height() const { return m_height; }
    WGPUTextureView texture() const { return m_target.view(); }
    ViewRect visibleWorld() const { return m_camera.visibleWorld(m_width, m_height); }

    void render(WGPUCommandEncoder enc, double budgetMs);
    void setHoldAllowed(bool allowed) { m_holdAllowed = allowed; }   // off: show updates in progress
    void paintOverlays(ImDrawList* dl, float originX, float originY);   // screen position of the view

    float clearColor[4] = { 0.10f, 0.10f, 0.10f, 1.0f };

private:
    static constexpr double kMaxHoldSeconds = 1.0;

    CanvasScene*    m_scene = nullptr;
    CanvasCamera    m_camera;
    OffscreenTarget m_target;
    float m_width = 0, m_height = 0;

    // what the target currently shows
    uint64_t m_renderedRevision = 0;   // never a valid scene revision
    Vec2     m_renderedPan{};
    float    m_renderedZoom = 0.0f;
    float    m_renderedW = 0, m_renderedH = 0;
    bool     m_targetNew = true;
    bool     m_holdAllowed = true;
    bool     m_holding   = false;   // content changed, the last complete picture is still shown
    std::chrono::steady_clock::time_point m_holdStart;
};
