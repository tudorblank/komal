#pragma once
// The Navigator window (Clip Studio style): the master's picture fitted to the window, the view
// outlined on it, zoom and rotation controls below.
//
//   drag the outline         moves the view with it
//   click / drag elsewhere   centres the view there, then follows the mouse
//   Ctrl+wheel on the map    zoom
//   zoom row                 - / + steps, a ramp slider, a field to type the percentage in
//   rotation row             15° steps either way, a wave slider, a field to type the degrees in
//
// The map is the master's bounds fitted to the window, proportions kept; the outline is clamped
// to the map's edges. The picture is a second view of the canvas's output tiles (RasterTiles),
// at the level of detail that fits: drawn on the GPU, rebuilt only where the output changed,
// and every tile it builds is one the canvas can reuse.

#include "canvas/items/checker.hpp"
#include "canvas/items/raster.hpp"
#include "canvas/view.hpp"

#include <imgui.h>

class ActionRegistry;
class Project;

class Navigator {
public:
    // After the canvas: draws its output tiles with its pipelines.
    void init(Project& project, RasterTiles& tiles, const CanvasPipelines& pipelines);
    void shutdown();

    // Before the canvas, so its changes show on the canvas the same frame.
    void draw(const CanvasNavigation& canvas, ActionRegistry& actions);
    bool& open() { return m_open; }   // shown (View > Navigator, the window's close button)
    void renderGpu(WGPUCommandEncoder enc);   // after the canvas's (which syncs the tiles)
    double nextFrameIn() const;               // tiles of the picture still coming in: now (Application::nextFrameIn)

private:
    static constexpr double kBuildBudgetMs = 2.0;   // per frame, for the picture's tiles
    static constexpr float  kCheckerPixels = 8.0f;

    void drawMap(ImVec2 size, const CanvasNavigation& canvas);
    void drawControls(const CanvasNavigation& canvas);

    Project*     m_project = nullptr;
    CanvasScene  m_scene;   // a checkerboard under the output's tiles
    CanvasView   m_view;    // the picture, sized and framed to the map's bounds
    CheckerItem* m_checker = nullptr;   // owned by m_scene
    bool         m_open = true;
    bool         m_ready = false;
    bool         m_pictureShown = false;   // drawn this frame: render it

    bool  m_dragging = false;
    Vec2  m_grab{};   // world offset from the mouse to the view's centre, while dragging
};
