#pragma once
#include "base/deadline.hpp"
#include "canvas/scene.hpp"
#include "gfx/render-target.hpp"


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

    void render(WGPUCommandEncoder enc, const Deadline& budget);   // building tiles until `budget` (CanvasEngine::budget)
    // Visible tiles still coming in: render() wants more frames.
    bool busy() const { return m_holding || (m_scene && !m_scene->settled()); }
    void drawOverlays(ImDrawList* dl, float originX, float originY);   // screen position of the view

private:
    static constexpr double kMaxHoldMs = 1000.0;

    CanvasScene*    m_scene = nullptr;
    CanvasCamera    m_camera;
    GpuUniform      m_cameraUniform;   // its view-projection, for the shaders
    WGPUQueue       m_queue = nullptr;
    RenderTarget m_target;
    float m_width = 0, m_height = 0;

    // what the target currently shows
    uint64_t m_renderedRevision = 0;   // never a valid scene revision
    Vec2     m_renderedPan{};
    float    m_renderedZoom = 0.0f;
    float    m_renderedRotation = 0.0f;
    float    m_renderedW = 0, m_renderedH = 0;
    bool     m_targetNew = true;
    bool     m_holding   = false;   // content changed, the last complete picture is still shown
    Deadline m_holdEnd;   // how long the last complete picture may stay
};
