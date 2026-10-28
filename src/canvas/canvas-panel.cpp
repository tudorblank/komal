#include "canvas-panel.hpp"
#include "core/project.hpp"
#include "gfx/device.hpp"
#include "ui/tools/tool-manager.hpp"

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
constexpr float kZBackground = -10.0f;   // the checkerboard: under everything
constexpr float kZOutput   =  0.0f;   // the edit preview takes the output's place
constexpr float kZOrigin   = -5.0f;   // the "+" on 0, 0: on the checkerboard, under the picture
constexpr float kZBounds   = 10.0f;

}

bool CanvasPanel::init(GpuDevice& gpu, Project& project, ToolManager& tools, const InputFrame& input, ActionRegistry& actions)
{
    m_project = &project;
    m_tools   = &tools;
    m_input   = &input;
    m_actions = &actions;
    m_ready = m_scene.init(GpuHandles{ gpu.device(), gpu.queue(), kFormat }) && m_view.init(m_scene);
    if (!m_ready) return false;
    actions.add({ .id = "canvas.frameAll", .title = "Frame All", .shortcuts = { { SDLK_F } }, .scope = ActionScope::Canvas,
                  .run = [this] { frameWorld(m_project->master().bounds()); } });   // lost? back to the picture

    CanvasCamera& cam = m_view.camera();
    cam.zoom = 2.0f;
    cam.pan  = { 300.0f, 160.0f };

    // The scene: the project's output (unbounded), overlays on top.
    m_scene.addItem<CheckerItem>()->setZ(kZBackground);   // transparency, wherever nothing covers it
    m_output = m_scene.addItem<RasterItem>(&project.master());
    m_output->setZ(kZOutput);
    m_preview = m_scene.addItem<EditPreviewItem>(project);
    m_preview->setZ(kZOutput);
    m_scene.addItem<OriginItem>()->setZ(kZOrigin);
    m_previewFrom = m_scene.addItem<OutlineItem>(IM_COL32(255, 255, 255, 90));
    m_previewFrom->setZ(kZBounds);
    m_previewTo = m_scene.addItem<OutlineItem>(IM_COL32(140, 180, 245, 255), 1.5f);
    m_previewTo->setZ(kZBounds);

    m_subscription = project.subscribe([this](const ProjectEvent& e) { onProjectEvent(e); });
    updateOutlines();
    return true;
}

void CanvasPanel::onProjectEvent(const ProjectEvent& e)
{
    switch (e.type) {
    case ProjectEventType::PreviewChanged:
        startPreview();
        updateOutlines();
        break;
    case ProjectEventType::NodeAdded:
    case ProjectEventType::NodeMoved:
        break;
    default:
        updateOutlines();
        break;
    }
}

void CanvasPanel::setSelection(std::vector<NodeId> ids)
{
    if (ids == m_selection) return;
    m_selection = std::move(ids);
    updateOutlines();
}

void CanvasPanel::shutdown()
{
    if (m_project && m_subscription) m_project->unsubscribe(m_subscription);
    m_subscription = 0;
    m_view.shutdown();
    m_scene.shutdown();
    m_output = nullptr;
    m_selectionBounds.clear();
    m_preview      = nullptr;
    m_previewFrom  = nullptr;
    m_previewTo    = nullptr;
    if (m_tools) m_tools->canvas() = {};
    m_ready = false;
}

void CanvasPanel::draw(bool* open)
{
    m_visible = false;
    m_window  = nullptr;
    m_hasPointer = false;

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

            // One item covers the canvas and owns the mouse while a button is held.
            ImGui::InvisibleButton("##canvas", avail, ImGuiButtonFlags_MouseButtonLeft |
                                                      ImGuiButtonFlags_MouseButtonRight |
                                                      ImGuiButtonFlags_MouseButtonMiddle);
            handleInput(origin);
            finishPreview();

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddImage((ImTextureID)(uintptr_t)m_view.texture(), origin, ImVec2(origin.x + avail.x, origin.y + avail.y));
            m_view.drawOverlays(dl, origin.x, origin.y);
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
        if (m_input->mods & ModPrimary) {
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
    m_hasPointer = hovered || active;
    m_tools->setSpringHand(m_hasPointer && !io.WantTextInput && m_input->isDown(SDLK_SPACE));
    if (m_hasPointer) m_actions->claimKeyboard(ActionScope::Canvas, KeyboardClaim::Pointer);   // [ ] brush size, ... next frame

    // The rest is the tool's: ImGui decides whether the pointer is ours, SDL gives every sample.
    Tool& tool = m_tools->active();
    m_pointer.clear();
    if (hovered || active || tool.busy())   // busy: a stroke still gets its release outside the canvas
        for (const PointerEvent& ev : m_input->pointer)
            m_pointer.push_back({ ev, cam.screenToWorld({ ev.x - origin.x, ev.y - origin.y }) });
    tool.canvasInput({ *m_project, cam, mouse, cam.screenToWorld(mouse), m_pointer, *m_input,
                       { m_view.width(), m_view.height() }, hovered, active });
}

void CanvasPanel::startPreview()
{
    const EditPreview& pv = m_project->preview();
    if (!pv.active()) { m_previewOutlined = false; return; }   // finishPreview() takes it down
    // Its GPU picture is planned once per node dragged.
    if (!m_previewOutlined && (!m_preview->active() || m_preview->node() != pv.node))
        m_previewOutlined = !m_preview->begin();
}

void CanvasPanel::finishPreview()
{
    // The drag ended: keep the preview until the output has caught up (no flash of the old picture).
    if (!m_project->preview().active() && m_preview->active() && m_output->settled()) m_preview->end();
    m_output->setSuppressed(m_preview->active() && m_preview->hasResult());
}

void CanvasPanel::updateOutlines()
{
    if (!m_ready) return;
    // Bounds of the nodes selected in the graph; with none selected, of the raster being edited.
    std::vector<BoundsI> outlined;
    for (NodeId id : m_selection)
        if (Node* n = m_project->find(id)) outlined.push_back(n->bounds());
    if (m_selection.empty())
        if (const RasterRootNode* active = m_project->activeRaster()) outlined.push_back(active->raster().getBounds());
    std::erase_if(outlined, [](const BoundsI& b) { return b.empty(); });
    while (m_selectionBounds.size() < outlined.size()) {
        m_selectionBounds.push_back(m_scene.addItem<OutlineItem>());
        m_selectionBounds.back()->setZ(kZBounds);
    }
    for (size_t i = 0; i < m_selectionBounds.size(); ++i) {
        m_selectionBounds[i]->setVisible(i < outlined.size());
        if (i < outlined.size()) m_selectionBounds[i]->setPixelBounds(outlined[i].minX, outlined[i].minY, outlined[i].maxX, outlined[i].maxY);
    }

    // A drag the GPU can't preview: outline the area it affects, and for a move where it goes.
    const EditPreview& pv = m_project->preview();
    Node* edited = m_previewOutlined && pv.active() ? m_project->find(pv.node) : nullptr;
    BoundsI from, to;
    if (auto* move = dynamic_cast<MoveNode*>(edited); move && pv.kind == EditPreview::Kind::MoveOffset) {
        if (move->input()) {
            const BoundsI in = move->input()->bounds();
            from = in.offsetBy(move->offsetX(), move->offsetY());
            to   = in.offsetBy(move->shownX(), move->shownY());
        }
    } else if (edited) {
        to = edited->bounds();
    }
    m_previewFrom->setVisible(!from.empty() && from != to);
    if (!from.empty()) m_previewFrom->setPixelBounds(from.minX, from.minY, from.maxX, from.maxY);
    m_previewTo->setVisible(!to.empty());
    if (!to.empty()) m_previewTo->setPixelBounds(to.minX, to.minY, to.maxX, to.maxY);
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

void CanvasPanel::frameWorld(const BoundsI& b)
{
    CanvasCamera& cam = m_view.camera();
    if (b.empty()) cam.centerOn(0.0f, 0.0f, m_view.width(), m_view.height());
    else           cam.frame(float(b.minX), float(b.minY), float(b.maxX + 1), float(b.maxY + 1), m_view.width(), m_view.height());
}

void CanvasPanel::renderGpu(WGPUCommandEncoder enc)
{
    if (!m_visible) return;
    // A stroke shows tile by tile as it's painted: holding the picture would make it jump.
    m_view.setHoldAllowed(!m_project->strokeActive());
    m_view.render(enc, kSyncBudgetMs);   // re-renders only when something changed
}
