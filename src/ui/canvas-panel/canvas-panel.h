#pragma once
#include "gfx/canvas-items.h"
#include "gfx/canvas-view.h"
#include "gfx/raster-item.h"
#include "ui/canvas-panel/edit-preview-item.h"
#include "ui/canvas-panel/tool-overlay-item.h"
#include "ui/tools/ring-cursor.h"
#include "ui/tools/tool.h"

#include <imgui.h>

#include <vector>

class GpuContext;
class Project;
class ToolManager;
struct ImDrawList;
struct ImGuiWindow;

// The raster canvas: a dockable window showing a CanvasScene through a CanvasView, an
// offscreen target displayed with an ImGui image. The scene holds the project's master output
// (RasterItem) under overlays (active raster bounds, the tool's cursor). While an edit is
// dragged in the graph, an EditPreviewItem shows it live in place of the output. The panel itself is navigation (MMB / wheel / Space) and
// hands the mouse to the active tool; tool settings live in the Tool Properties window.
class CanvasPanel {
public:
    bool init(GpuContext& gpu, Project& project, ToolManager& tools, const InputFrame& input);
    void shutdown();

    void draw(bool* open);                    // ImGui side: window, input, image, overlays
    SDL_Cursor* cursorOverride() const { return m_cursorOverride; }   // the tool's system cursor, this frame
    // A file dropped at `screen` with `window` topmost there: true if it lands on the canvas,
    // with the world point under it.
    bool dropTarget(const ImGuiWindow* window, ImVec2 screen, Vec2& world) const;
    void renderGpu(WGPUCommandEncoder enc);   // wgpu side: the view renders if anything changed

private:
    void handleInput(const ImVec2& origin);
    void updatePreview();
    void updateOverlayItems(const ImVec2& origin);
    void drawHud(ImDrawList* dl, const ImVec2& origin);

    Project*     m_project = nullptr;
    ToolManager* m_tools   = nullptr;
    CanvasScene  m_scene;
    CanvasView   m_view;
    RasterItem*      m_output       = nullptr;   // owned by m_scene
    OutlineItem*     m_activeBounds = nullptr;
    EditPreviewItem* m_preview      = nullptr;   // an edit being dragged in the graph, live on the GPU
    OutlineItem*     m_previewFrom  = nullptr;   // ... or, where the GPU can't show it: where it was
    OutlineItem*     m_previewTo    = nullptr;   //     and where it will land
    bool             m_previewOutlined = false;  // this drag is shown with outlines
    ToolOverlayItem* m_toolOverlay  = nullptr;

    const InputFrame* m_input = nullptr;   // the app's pointer input, this frame
    RingCursors  m_ringCursors;
    SDL_Cursor*  m_cursorOverride = nullptr;
    std::vector<CanvasPointer> m_pointer;  // ... the part the canvas gets, in world space

    const ImGuiWindow* m_window = nullptr;   // this frame's, while the canvas is shown
    ImVec2 m_canvasMin{}, m_canvasMax{};
    bool m_visible = false;   // false when collapsed / tabbed away
    bool m_ready   = false;
};
