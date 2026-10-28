#pragma once
#include "canvas/items/checker.hpp"
#include "canvas/items/origin.hpp"
#include "canvas/items/outline.hpp"
#include "canvas/view.hpp"
#include "canvas/items/raster.hpp"
#include "canvas/items/preview.hpp"
#include "ui/tools/tool.hpp"

#include <imgui.h>

#include <vector>

class GpuDevice;
class Project;
struct ProjectEvent;
class ToolManager;
struct ImDrawList;
struct ImGuiWindow;

// The canvas window: a CanvasScene seen through a CanvasView (an offscreen target shown as an
// ImGui image). The scene holds the master output under overlays (selected nodes' bounds); an
// edit dragged in the graph replaces the output with an EditPreviewItem.
// The panel navigates (MMB / wheel / Space) and hands the mouse to the active tool.
//
// It follows the project like the graph does: the output picture through the master's dirty
// tiles, everything else through Project events, which update its items:
//
//   PreviewChanged      the preview item starts (or stops), outlines follow the edit
//   NodeAdded / Moved   nothing (the graph editor's business)
//   anything else       outlines: a node's bounds may have changed
class CanvasPanel {
public:
    bool init(GpuDevice& gpu, Project& project, ToolManager& tools, const InputFrame& input, ActionRegistry& actions);
    void shutdown();

    void draw(bool* open);                    // ImGui side: window, input, image, overlays
    // A file dropped at `screen` (`window` topmost): true if it lands on the canvas, with its world point.
    bool dropTarget(const ImGuiWindow* window, ImVec2 screen, Vec2& world) const;
    void renderGpu(WGPUCommandEncoder enc);   // wgpu side: the view renders if anything changed
    // Fits world rectangle `b`; empty: centres the origin.
    void frameWorld(const BoundsI& b);
    bool busy() const { return m_visible && m_view.busy(); }   // still catching up: needs more frames
    // The nodes selected in the graph: their bounds are outlined (none: the active raster's).
    void setSelection(std::vector<NodeId> ids);

private:
    void onProjectEvent(const ProjectEvent& e);
    void handleInput(const ImVec2& origin);
    void startPreview();         // a drag started or moved on to another node
    void finishPreview();        // once a drag ended, when the output has caught up
    void updateOutlines();       // the selected nodes' bounds and the edit's
    void drawHud(ImDrawList* dl, const ImVec2& origin);

    Project*     m_project = nullptr;
    int          m_subscription = 0;
    ToolManager* m_tools   = nullptr;
    CanvasScene  m_scene;
    CanvasView   m_view;
    RasterItem*      m_output       = nullptr;   // owned by m_scene
    std::vector<OutlineItem*> m_selectionBounds;   // one per outlined node, spares hidden
    std::vector<NodeId>       m_selection;
    EditPreviewItem* m_preview      = nullptr;   // an edit being dragged in the graph, live on the GPU
    OutlineItem*     m_previewFrom  = nullptr;   // ... or, where the GPU can't show it: where it was
    OutlineItem*     m_previewTo    = nullptr;   //     and where it will land
    bool             m_previewOutlined = false;  // this drag is shown with outlines

    const InputFrame* m_input = nullptr;   // the app's pointer input, this frame
    ActionRegistry*   m_actions = nullptr; // canvas-scoped shortcuts fire here, while it has the pointer
    std::vector<CanvasPointer> m_pointer;  // ... the part the canvas gets, in world space

    const ImGuiWindow* m_window = nullptr;   // this frame's, while the canvas is shown
    ImVec2 m_canvasMin{}, m_canvasMax{};
    bool m_visible = false;   // false when collapsed / tabbed away
    bool m_hasPointer = false;
    bool m_ready   = false;
};
