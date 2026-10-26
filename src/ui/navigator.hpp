#pragma once
// The Navigator window (Clip Studio style): the master's picture fitted to the window, the view
// outlined on it, zoom and rotation controls below.
//
//   drag the outline         moves the view with it
//   click / drag elsewhere   centres the view there, then follows the mouse
//   Ctrl+wheel on the map    zoom
//   zoom row                 - / + steps, a slider, a field to type the percentage in
//   rotation row             15° steps either way, a slider, a field to type the degrees in
//
// The map is the master's bounds fitted to the window, proportions kept; the outline is clamped
// to the map's edges. The picture renders at its shown size from the master's cache, only when
// the master changed (throttled, never mid-stroke) or the window settled at a new size.

#include "canvas/camera.hpp"
#include "node/node.hpp"

#include <imgui.h>
#include <webgpu/webgpu.h>
#include <vector>

class GpuDevice;
class Project;
struct CanvasNavigation;

class Navigator {
public:
    void init(GpuDevice& gpu, Project& project);
    void shutdown();

    // Before the canvas, so its changes show on the canvas the same frame.
    void draw(bool* open, CanvasNavigation& canvas);
    bool busy() const { return m_pending; }   // the picture is due a render

private:
    static constexpr int    kMaxPicture  = 1024;          // pixels, either side
    static constexpr double kMinInterval = 1.0 / 12.0;    // seconds between renders while the master changes
    static constexpr double kSettle      = 0.15;          // seconds the map's size must hold before a re-render

    void drawMap(ImVec2 size, CanvasNavigation& canvas);
    void drawControls(CanvasNavigation& canvas);
    void updatePicture(const BoundsI& bounds, float areaW, float areaH);   // the room it may fill, pixels
    void renderPicture(Node& master, int width, int height);

    Project*   m_project = nullptr;
    WGPUDevice m_device  = nullptr;
    WGPUQueue  m_queue   = nullptr;

    // The picture: the top-left m_picW x m_picH of one max-size texture (no re-creation on resize).
    WGPUTexture     m_texture = nullptr;
    WGPUTextureView m_view    = nullptr;
    int      m_picW = 0, m_picH = 0;
    BoundsI  m_picBounds;                // the master's bounds it shows: the map is laid out from these
    uint64_t m_picRevision = ~0ull;
    double   m_renderedAt  = -1.0;
    int      m_wantW = 0, m_wantH = 0;   // the size asked for, and since when
    double   m_wantSince = 0.0;
    bool     m_pending = false;
    RenderCtx             m_render;
    std::vector<RGBA> m_pixels;

    bool  m_dragging = false;
    Vec2  m_grab{};   // world offset from the mouse to the view's centre, while dragging
};
