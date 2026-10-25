#include "canvas-panel.h"
#include "core/project.h"
#include "gfx/gpu-context.h"
#include "ui/tools/tool-manager.h"

#include <imgui.h>
#include <imgui_internal.h>   // ImGuiWindow: which window a drop landed on
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

constexpr WGPUTextureFormat kFormat       = WGPUTextureFormat_RGBA8Unorm;
constexpr double            kSyncBudgetMs = 4.0;    // per frame, leaves room for the rest of the frame
constexpr float             kWheelPanStep = 24.0f;  // points per wheel notch
constexpr float             kZoomStep     = 1.1f;   // per Ctrl+wheel notch

// z order of the viewport's items
constexpr float kZOutput   =  0.0f;   // the edit preview takes the output's place
constexpr float kZBounds   = 10.0f;
constexpr float kZTool     = 20.0f;

} // namespace

bool CanvasPanel::init(GpuContext& gpu, Project& project, ToolManager& tools)
{
    m_project = &project;
    m_tools   = &tools;
    m_ready = m_scene.init(GfxContext{ gpu.device(), gpu.queue(), kFormat }) && m_view.init(m_scene);
    if (!m_ready) return false;

    CanvasCamera& cam = m_view.camera();
    cam.zoom = 2.0f;
    cam.pan  = { 300.0f, 160.0f };

    // The scene: the project's output (the raster is unbounded: no document rectangle), and
    // overlays on top.
    m_output = m_scene.addItem<RasterItem>(&project.master());
    m_output->setZ(kZOutput);
    m_preview = m_scene.addItem<EditPreviewItem>(project);
    m_preview->setZ(kZOutput);
    m_activeBounds = m_scene.addItem<OutlineItem>();
    m_activeBounds->setZ(kZBounds);
    m_previewFrom = m_scene.addItem<OutlineItem>(IM_COL32(255, 255, 255, 90));
    m_previewFrom->setZ(kZBounds);
    m_previewTo = m_scene.addItem<OutlineItem>(IM_COL32(84, 140, 242, 255), 1.5f);
    m_previewTo->setZ(kZBounds);
    m_toolOverlay = m_scene.addItem<ToolOverlayItem>(tools);
    m_toolOverlay->setZ(kZTool);
    return true;
}

void CanvasPanel::shutdown()
{
    m_view.shutdown();
    m_scene.shutdown();
    m_output = nullptr;
    m_activeBounds = nullptr;
    m_preview      = nullptr;
    m_previewFrom  = nullptr;
    m_previewTo    = nullptr;
    m_toolOverlay = nullptr;
    if (m_tools) m_tools->canvas() = {};
    m_ready = false;
}

void CanvasPanel::draw(bool* open)
{
    m_visible = false;
    m_window  = nullptr;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool shown = ImGui::Begin("Canvas", open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    m_tools->canvas() = {};
    if (shown && m_ready) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail  = ImGui::GetContentRegionAvail();
        const ImVec2 fb     = ImGui::GetIO().DisplayFramebufferScale;   // HiDPI: render at native pixels
        if (avail.x >= 1.0f && avail.y >= 1.0f && m_view.resize(avail.x, avail.y, fb.x, fb.y)) {
            m_visible = true;
            m_window  = ImGui::GetCurrentWindowRead();
            m_canvasMin = origin;
            m_canvasMax = ImVec2(origin.x + avail.x, origin.y + avail.y);
            m_tools->canvas() = { &m_view.camera(), avail.x, avail.y };

            // One item covers the canvas: it owns the mouse while a button is held, so drags
            // never move the window or reach widgets behind it.
            ImGui::InvisibleButton("##canvas", avail, ImGuiButtonFlags_MouseButtonLeft |
                                                      ImGuiButtonFlags_MouseButtonRight |
                                                      ImGuiButtonFlags_MouseButtonMiddle);
            handleInput(origin);
            updatePreview();
            updateOverlayItems(origin);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddImage((ImTextureID)(uintptr_t)m_view.texture(), origin, ImVec2(origin.x + avail.x, origin.y + avail.y));
            m_view.paintOverlays(dl, origin.x, origin.y);
            drawHud(dl, origin);
        }
    }
    ImGui::End();
}

bool CanvasPanel::dropTarget(const ImGuiWindow* window, ImVec2 screen, Vec2& world) const
{
    if (!m_visible || !window || window->RootWindow != m_window) return false;
    if (screen.x < m_canvasMin.x || screen.y < m_canvasMin.y || screen.x >= m_canvasMax.x || screen.y >= m_canvasMax.y) return false;
    world = m_view.camera().screenToWorld({ screen.x - m_canvasMin.x, screen.y - m_canvasMin.y });
    return true;
}

void CanvasPanel::handleInput(const ImVec2& origin)
{
    ImGuiIO& io = ImGui::GetIO();
    CanvasCamera& cam = m_view.camera();
    const Vec2 mouse = { io.MousePos.x - origin.x, io.MousePos.y - origin.y };
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();

    // Navigation that works whatever the tool.
    if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)) {
        if (io.KeyCtrl) {
            cam.zoomAt(mouse, std::pow(kZoomStep, io.MouseWheel));
        } else {
            cam.pan.x += io.MouseWheelH * kWheelPanStep;
            cam.pan.y += io.MouseWheel  * kWheelPanStep;
        }
    }
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Middle) && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        cam.pan.x += io.MouseDelta.x;
        cam.pan.y += io.MouseDelta.y;
    }
    m_tools->setSpringHand((hovered || active) && !io.WantTextInput && ImGui::IsKeyDown(ImGuiKey_Space));

    // The rest is the tool's.
    m_trail.clear();
    for (const ImVec2& p : m_tools->mouseTrail()) m_trail.push_back(cam.screenToWorld({ p.x - origin.x, p.y - origin.y }));
    Tool& tool = m_tools->active();
    tool.canvasInput({ *m_project, cam, mouse, cam.screenToWorld(mouse), m_trail, { m_view.width(), m_view.height() }, hovered, active });
    if (hovered || active) ImGui::SetMouseCursor(tool.cursor());
}

void CanvasPanel::updatePreview()
{
    const EditPreview& pv = m_project->preview();
    if (pv.active()) {
        // A drag started (or moved on to another node): plan its GPU picture once.
        if (!m_previewOutlined && (!m_preview->active() || m_preview->node() != pv.node))
            m_previewOutlined = !m_preview->begin();
    } else {
        m_previewOutlined = false;
        // The drag ended and the graph now has the value: keep showing the preview until the
        // output has caught up on screen, then hand back to it (no flash of the old picture).
        if (m_preview->active() && m_output->settled()) m_preview->end();
    }
    m_output->setSuppressed(m_preview->active() && m_preview->hasResult());
}

void CanvasPanel::updateOverlayItems(const ImVec2& origin)
{
    // Bounds of the raster being edited.
    const RasterRootNode* active = m_project->activeRaster();
    const bool hasBounds = active && !active->raster().getBounds().empty();
    m_activeBounds->setVisible(hasBounds);
    if (hasBounds) {
        const BoundsI& b = active->raster().getBounds();
        m_activeBounds->setPixelBounds(b.minX, b.minY, b.maxX, b.maxY);
    }

    // A drag the GPU can't preview: outline the area it affects, and for a move where it goes.
    const EditPreview& pv = m_project->preview();
    Node* edited = m_previewOutlined && pv.active() ? m_project->find(pv.node) : nullptr;
    BoundsI from, to;
    if (auto* move = dynamic_cast<MoveNode*>(edited); move && pv.kind == EditPreview::Kind::MoveOffset) {
        if (move->input()) {
            const BoundsI in = move->input()->bounds();
            from = in.offsetBy(move->offsetX(), move->offsetY());
            to   = in.offsetBy(pv.x, pv.y);
        }
    } else if (edited) {
        to = edited->bounds();
    }
    m_previewFrom->setVisible(!from.empty() && from != to);
    if (!from.empty()) m_previewFrom->setPixelBounds(from.minX, from.minY, from.maxX, from.maxY);
    m_previewTo->setVisible(!to.empty());
    if (!to.empty()) m_previewTo->setPixelBounds(to.minX, to.minY, to.maxX, to.maxY);

    // The tool's cursor while the mouse is over the canvas (or holds it).
    const bool showCursor = ImGui::IsItemHovered() || ImGui::IsItemActive();
    m_toolOverlay->setVisible(showCursor);
    if (showCursor) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const Vec2 w = m_view.camera().screenToWorld({ m.x - origin.x, m.y - origin.y });
        m_toolOverlay->setMouse(w.x, w.y);
    }
}

void CanvasPanel::drawHud(ImDrawList* dl, const ImVec2& origin)
{
    const RasterRootNode* active = m_project->activeRaster();
    const bool shown = active && m_project->master().dependsOn(active);
    char hud[256];
    std::snprintf(hud, sizeof hud, "%.0f%%   %s   painting: %s%s%s   level %d: %zu tiles / %zu atlas pages%s",
                  m_view.camera().zoom * 100.0f, m_tools->active().name(),
                  active ? active->m_meta.label.c_str() : "nothing (click a raster node)",
                  active && active->locked() ? " (locked)" : "", active && !shown ? " (not in Master)" : "",
                  m_output->drawLevel(), m_output->chunkCount(), m_output->pageCount(), m_output->syncPending() ? " (syncing)" : "");
    const ImVec2 pad(8.0f, 6.0f);
    const ImVec2 size = ImGui::CalcTextSize(hud);
    const ImVec2 pos(origin.x + pad.x, origin.y + m_view.height() - size.y - pad.y * 2.0f);
    dl->AddRectFilled(ImVec2(pos.x - 4.0f, pos.y - 2.0f), ImVec2(pos.x + size.x + 4.0f, pos.y + size.y + 2.0f),
                      IM_COL32(0, 0, 0, 140), 3.0f);
    dl->AddText(pos, IM_COL32(220, 220, 220, 255), hud);
}

void CanvasPanel::renderGpu(WGPUCommandEncoder enc)
{
    if (!m_visible) return;
    // A stroke shows up as it is painted, tile by tile: holding the picture back until it had
    // caught up would make it appear in jumps.
    m_view.setHoldAllowed(!m_project->strokeActive());
    m_view.render(enc, kSyncBudgetMs);   // re-renders only when something changed
}
