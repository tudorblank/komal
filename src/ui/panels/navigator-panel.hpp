#pragma once

#include "ui/panels/canvas/items/checker.hpp"
#include "ui/panels/canvas/items/raster.hpp"
#include "ui/panels/canvas/preview/preview-picture.hpp"
#include "ui/panels/canvas/view.hpp"
#include "ui/panels/panel-base.hpp"

#include <imgui.h>

class Navigator : public Panel {
public:
    PanelInfo info() const override;
    bool init() override;   // draws the engine's output tiles with its pipelines
    void shutdown() override;
    void releaseGpu() override;
    void restoreGpu() override;

    void draw() override;
    void renderGpu(WGPUCommandEncoder enc) override;
    Clock::Seconds nextFrameIn() const override;        // tiles of the picture (or the preview's) still coming in: now

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
