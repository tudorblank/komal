#pragma once
#include "base/signal.hpp"
#include "ui/panels/canvas/items/artboard-frame.hpp"
#include "ui/panels/canvas/items/checker.hpp"
#include "ui/panels/canvas/items/origin.hpp"
#include "ui/panels/canvas/items/outline.hpp"
#include "ui/panels/canvas/view.hpp"
#include "ui/panels/canvas/items/raster.hpp"
#include "ui/panels/canvas/preview/preview-item.hpp"
#include "ui/panels/panel-base.hpp"
#include "ui/panels/panel-surface.hpp"
#include "ui/tools/tool-base.hpp"

#include <imgui.h>

#include <memory>
#include <vector>

class Workspace;
struct WorkspaceEvent;
class ToolManager;
class Painter;
struct ImGuiWindow;

// The canvas window: one canvas per open document, the active document's shown (the documents'
// tabs are their own strip: DocumentTabsPanel). A canvas is a CanvasScene seen through a CanvasView
// (an offscreen target shown as an ImGui image). The scene holds the master output under overlays
// (selected nodes' bounds); an edit in progress (a drag in the graph, a stroke) shows in its place
// on the GPU (PreviewItem). The panel navigates (MMB / wheel / Space) and hands the mouse to the
// active tool.
//
// A canvas changes only with its workspace, like the graph: the output picture through the master's
// dirty tiles, everything else through Workspace events, which update its items:
//
//   CanvasViewChanged   the view follows the workspace's camera (Workspace::canvasCamera())
//   PreviewChanged      outlines follow the edit (where the canvas can't show it)
//   NodeAdded / Moved   nothing (the graph editor's business)
//   anything else       outlines: the selection, or a node's bounds, may have changed
// Its own navigation (wheel, middle drag) and the tools' go through the canvas.* actions too, and it
// tells the workspace its size (canvas.viewport).
class CanvasPanel : public Panel {
public:
    PanelInfo info() const override;
    bool init() override;   // the active document's canvas, drawing the engine's tiles with its pipelines
    void shutdown() override;
    void releaseGpu() override;
    void restoreGpu() override;
    void documentsChanged(const DocumentsEvent& e) override;   // a canvas made as one is shown, gone as it closes

    void draw() override;   // window, input, image, overlays
    // The view renders if anything changed (the engine synced the tiles before).
    void renderGpu(WGPUCommandEncoder enc) override;
    Clock::Seconds nextFrameIn() const override { return busy() ? kFrameNow : kNoFrame; }
    // A file dropped at `screen` (`window` topmost): true if it lands on the canvas, with its world point.
    bool dropTarget(const ImGuiWindow* window, ImVec2 screen, Vec2& world) const;
    bool busy() const { return m_visible && m_canvas && m_canvas->view.busy(); }   // still catching up: needs more frames

private:
    // One document's canvas: made from the engine's pictures of it while it's active.
    struct Canvas {
        DocumentId       document = kNoDocument;
        const Workspace* workspace = nullptr;
        CanvasScene  scene;
        CanvasView   view;
        RasterItem*      output       = nullptr;   // owned by scene
        std::vector<OutlineItem*> selectionBounds;   // one per outlined node, spares hidden
        std::vector<ArtboardFrameItem*> artboards;   // one per artboard, spares hidden
        const Preview*   preview      = nullptr;   // the edit in progress (the engine's): bounds under it
        PreviewItem*     previewItem  = nullptr;   // ... live on the GPU, in the scene
        OutlineItem*     previewFrom  = nullptr;   // ... or, a drag that doesn't reach the master: where it was
        OutlineItem*     previewTo    = nullptr;   //     and where it will land
        Connection       events;                   // its workspace's changed
        bool             ready = false;
    };

    Canvas* canvasOf(DocumentId id);
    void show(DocumentId id);    // the active one: its canvas, made if it's new
    bool makeGpu(Canvas& c);     // its scene and view on the engine's pipelines
    void onWorkspaceEvent(Canvas& c, const WorkspaceEvent& e);
    void tellSize(Vec2 size);    // the workspace hears its size when it changes ({0, 0}: not shown)
    void handleInput(const ImVec2& origin);
    void updateOutlines(Canvas& c);   // the selected nodes' bounds and the edit's
    void drawHud(const Painter& draw, const ImVec2& origin);

    std::vector<std::unique_ptr<Canvas>> m_canvases;   // the documents shown so far
    Canvas* m_canvas = nullptr;                        // the active one's

    std::vector<CanvasPointer> m_pointer;  // ... the part the canvas gets, in world space

    PanelSurface m_surface;   // the picture, this frame
    bool m_visible = false;   // false when closed, collapsed or tabbed away
    bool m_hasPointer = false;
};
