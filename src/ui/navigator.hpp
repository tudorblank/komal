#pragma once
// The Navigator window (Clip Studio style): the master's picture fitted to the window, the view
// outlined on it, zoom and rotation controls below.
//
//   drag the outline         moves the view with it
//   click / drag elsewhere   centres the view there, then follows the mouse
//   Ctrl / Cmd+wheel on map  zoom
//   zoom row                 - / + steps, a ramp slider, a field to type the percentage in
//   rotation row             15° steps either way, a wave slider, a field to type the degrees in
//
// It reads the canvas view from the project (Project::canvasCamera(), canvasViewport()) and moves
// it only through the canvas.* actions, like every other view.
//
// The map is the master's bounds fitted to the window, proportions kept; the outline is clamped
// to the map's edges. The picture is a second view of the master's output tiles (the engine's
// TileCache), at the level of detail that fits: drawn on the GPU, rebuilt only where the
// output changed, and every tile it builds is one the canvas can reuse. While an edit in progress
// reaches the master, the map shows the preview's picture of it instead (PreviewPicture, the
// preview's thumbnail branch), until its own picture has caught up with the edit's result.

#include "canvas/items/checker.hpp"
#include "canvas/items/raster.hpp"
#include "canvas/preview/preview-picture.hpp"
#include "canvas/view.hpp"
#include "ui/panel.hpp"

#include <imgui.h>

class Navigator : public Panel {
public:
    PanelInfo info() const override;
    bool init() override;   // draws the engine's output tiles with its pipelines
    void shutdown() override;

    void draw() override;
    void renderGpu(WGPUCommandEncoder enc) override;
    double nextFrameIn() const override;        // tiles of the picture (or the preview's) still coming in: now

private:
    bool showsPreview();   // the preview's picture of the master, rather than the view's (draw time)
    void drawMap(ImVec2 size);
    void drawControls();
    void zoomBy(float factor);   // about the canvas's middle
    void rotateTo(float degrees);

    CanvasScene  m_scene;   // a checkerboard under the output's tiles
    CanvasView   m_view;    // the picture, sized and framed to the map's bounds
    CheckerItem* m_checker = nullptr;   // owned by m_scene
    bool         m_ready = false;
    bool         m_pictureShown = false;   // drawn this frame: render it

    const Preview*         m_preview   = nullptr;   // the engine's
    const CanvasPipelines* m_pipelines = nullptr;
    PreviewPicture m_previewPicture;    // the master under the edit in progress
    uint32_t m_previewW = 0, m_previewH = 0;   // what it's fitted into, pixels (the map, as drawn)
    bool     m_previewWaiting = false;  // its leaves still coming in
    bool     m_handoff = false;         // it was shown: shown until the view has caught up
    int      m_handoffFrames = 0;       // since the edit ended

    bool  m_dragging = false;
    Vec2  m_grab{};   // world offset from the mouse to the view's centre, while dragging
    float m_rotationShown = 0.0f;   // the rotation slider's value last frame (which end 180 is at)
};
