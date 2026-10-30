#pragma once
#include "base/signal.hpp"
#include "canvas/items/checker.hpp"
#include "canvas/items/origin.hpp"
#include "canvas/items/outline.hpp"
#include "canvas/view.hpp"
#include "canvas/items/raster.hpp"
#include "canvas/preview/preview-item.hpp"
#include "ui/panel.hpp"
#include "ui/panel-surface.hpp"
#include "ui/tools/tool.hpp"

#include <imgui.h>

#include <vector>

class Project;
class TileCache;
struct ProjectEvent;
class ToolManager;
struct ImDrawList;
struct ImGuiWindow;

// The canvas window: a CanvasScene seen through a CanvasView (an offscreen target shown as an
// ImGui image). The scene holds the master output under overlays (selected nodes' bounds); an
// edit in progress (a drag in the graph, a stroke) shows in its place on the GPU (PreviewItem).
// The panel navigates (MMB / wheel / Space) and hands the mouse to the active tool.
//
// It changes only with the project, like the graph: the output picture through the master's dirty
// tiles, everything else through Project events, which update its items:
//
//   CanvasViewChanged   the view follows the project's camera (Project::canvasCamera())
//   PreviewChanged      outlines follow the edit (where the canvas can't show it)
//   NodeAdded / Moved   nothing (the graph editor's business)
//   anything else       outlines: the selection, or a node's bounds, may have changed
// Its own navigation (wheel, middle drag) and the tools' go through the canvas.* actions too, and it
// tells the project its size (canvas.viewport).
class CanvasPanel : public Panel {
public:
    PanelInfo info() const override;
    bool init() override;   // draws the engine's output tiles with its pipelines
    void shutdown() override;

    void draw() override;   // window, input, image, overlays
    // The view renders if anything changed (the engine synced the tiles before).
    void renderGpu(WGPUCommandEncoder enc) override;
    double nextFrameIn() const override { return busy() ? 0.0 : kNoFrame; }
    // A file dropped at `screen` (`window` topmost): true if it lands on the canvas, with its world point.
    bool dropTarget(const ImGuiWindow* window, ImVec2 screen, Vec2& world) const;
    bool busy() const { return m_visible && m_view.busy(); }   // still catching up: needs more frames

private:
    void onProjectEvent(const ProjectEvent& e);
    void tellSize(Vec2 size);   // the project hears its size when it changes ({0, 0}: not shown)
    void handleInput(const ImVec2& origin);
    void updateOutlines();       // the selected nodes' bounds and the edit's
    void drawHud(ImDrawList* dl, const ImVec2& origin);

    Connection     m_projectEvents;   // project.changed
    const TileCache* m_tiles = nullptr;   // the engine's: every view's GPU tiles, for the HUD
    CanvasScene  m_scene;
    CanvasView   m_view;
    RasterItem*      m_output       = nullptr;   // owned by m_scene
    std::vector<OutlineItem*> m_selectionBounds;   // one per outlined node, spares hidden
    const Preview*   m_preview      = nullptr;   // the edit in progress (the engine's): bounds under it
    PreviewItem*     m_previewItem  = nullptr;   // ... live on the GPU, in the scene
    OutlineItem*     m_previewFrom  = nullptr;   // ... or, a drag that doesn't reach the master: where it was
    OutlineItem*     m_previewTo    = nullptr;   //     and where it will land

    std::vector<CanvasPointer> m_pointer;  // ... the part the canvas gets, in world space

    PanelSurface m_surface;   // the picture, this frame
    bool m_visible = false;   // false when closed, collapsed or tabbed away
    bool m_hasPointer = false;
    bool m_ready   = false;
};
