#include "canvas-panel.hpp"
#include "ui/input-owner.hpp"
#include "document/document-actions.hpp"
#include "ui/render/painter.hpp"
#include "ui/style/ui-style.hpp"
#include "ui/panels/canvas/canvas-engine.hpp"
#include "ui/panels/canvas/imgui-texture.hpp"
#include "document/project.hpp"
#include "ui/navigation.hpp"
#include "ui/tools/tool-manager.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // ImGuiWindow: which window a drop landed on
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {


// z order of the viewport's items
constexpr float kZBackground = -10.0f;   // the checkerboard: under everything
constexpr float kZOutput   =  0.0f;   // the edit preview takes the output's place
constexpr float kZOrigin   = -5.0f;   // the "+" on 0, 0: on the checkerboard, under the picture
constexpr float kZBounds   = 10.0f;

}

PanelInfo CanvasPanel::info() const { return { Actions::ViewCanvas, "Canvas", DockSlot::Center, DrawPhase::Show }; }

bool CanvasPanel::init()
{
    const Project& project = ui().project;
    m_tiles   = &ui().canvas.tiles();
    m_ready = ui().canvas.ready() && m_scene.init(ui().canvas.pipelines()) && m_view.init(m_scene);
    if (!m_ready) return false;
    m_view.camera() = project.canvasCamera();

    // The scene: the project's output (unbounded), overlays on top.
    m_scene.addItem<CheckerItem>()->setZ(kZBackground);   // transparency, wherever nothing covers it
    m_output = m_scene.addItem<RasterItem>(ui().canvas.outputTiles());
    m_output->setZ(kZOutput);
    m_preview     = &ui().canvas.preview();
    m_previewItem = m_scene.addItem<PreviewItem>(ui().canvas.preview(), *m_output);
    m_previewItem->setZ(kZOutput);
    m_scene.addItem<OriginItem>()->setZ(kZOrigin);
    m_previewFrom = m_scene.addItem<OutlineItem>(OutlineItem::Kind::PreviewFrom);
    m_previewFrom->setZ(kZBounds);
    m_previewTo = m_scene.addItem<OutlineItem>(OutlineItem::Kind::PreviewTo);
    m_previewTo->setZ(kZBounds);

    m_projectEvents = project.changed.connect([this](const ProjectEvent& e) { onProjectEvent(e); });
    updateOutlines();
    return true;
}

void CanvasPanel::onProjectEvent(const ProjectEvent& e)
{
    switch (e.type) {
    case ProjectEventType::CanvasViewChanged:
        m_view.camera() = ui().project.canvasCamera();
        break;
    case ProjectEventType::PreviewChanged:
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

void CanvasPanel::releaseGpu()
{
    m_view.shutdown();
    m_scene.releaseGpu();
    m_ready = false;
}

void CanvasPanel::restoreGpu()
{
    if (!m_output) {   // never made (no GPU at start): made now
        init();
        return;
    }
    m_ready = ui().canvas.ready() && m_scene.init(ui().canvas.pipelines()) && m_view.init(m_scene);
    if (m_ready) m_view.camera() = ui().project.canvasCamera();
}

void CanvasPanel::shutdown()
{
    m_projectEvents.disconnect();
    m_view.shutdown();
    m_scene.shutdown();
    m_output = nullptr;
    m_selectionBounds.clear();
    m_artboards.clear();
    m_previewItem  = nullptr;
    m_previewFrom  = nullptr;
    m_previewTo    = nullptr;
    m_ready = false;
}

void CanvasPanel::draw()
{
    m_visible = false;
    m_surface.hide();
    m_hasPointer = false;
    if (!isOpen()) {
        tellSize({});
        return;
    }

    const Widgets::PanelWindow window(*this, ui().actions, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse,
                                      Widgets::PanelWindow::Padding::None);
    if (window && m_ready) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail  = ImGui::GetContentRegionAvail();
        const ImVec2 fb     = ImGui::GetIO().DisplayFramebufferScale;   // HiDPI: render at native pixels
        if (avail.x >= 1.0f && avail.y >= 1.0f && m_view.resize(avail.x, avail.y, fb.x, fb.y)) {
            m_visible = true;
            m_surface.show(ImGui::GetCurrentWindowRead(), origin, avail);
            tellSize({ avail.x, avail.y });   // before input: what it frames is this frame's size

            // One item covers the canvas and owns the mouse while a button is held.
            ImGui::InvisibleButton("##canvas", avail, ImGuiButtonFlags_MouseButtonLeft |
                                                      ImGuiButtonFlags_MouseButtonRight |
                                                      ImGuiButtonFlags_MouseButtonMiddle);
            handleInput(origin);

            const Painter draw = Painter::window();
            draw.image(imguiTexture(m_view.texture()), origin, ImVec2(origin.x + avail.x, origin.y + avail.y));
            m_view.drawOverlays(draw, origin.x, origin.y);
            drawHud(draw, origin);
        }
    }
    if (!m_visible) tellSize({});   // collapsed, tabbed away
}

void CanvasPanel::tellSize(Vec2 size)
{
    if (size != ui().project.canvasViewport()) ui().actions.run(Actions::CanvasViewport, { { DocArgs::kWidth, size.x }, { DocArgs::kHeight, size.y } });
}

bool CanvasPanel::dropTarget(const ImGuiWindow* window, ImVec2 screen, Vec2& world) const
{
    if (!m_surface.contains(window, screen)) return false;
    const ImVec2 p = m_surface.local(screen);
    world = m_view.camera().screenToWorld({ p.x, p.y });
    return true;
}

void CanvasPanel::handleInput(const ImVec2& origin)
{
    ImGuiIO& io = ImGui::GetIO();
    const CanvasCamera& cam = m_view.camera();
    const Vec2 mouse = { io.MousePos.x - origin.x, io.MousePos.y - origin.y };
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();

    // Navigation that works whatever the tool.
    if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)) {
        if (ui().input.mods & ModPrimary)
            ui().actions.run(Actions::CanvasZoom, { { DocArgs::kFactor, std::pow(Navigation::kWheelZoomStep, io.MouseWheel) }, { DocArgs::kX, mouse.x }, { DocArgs::kY, mouse.y } });
        else
            ui().actions.run(Actions::CanvasPan, { { DocArgs::kDx, io.MouseWheelH * Navigation::kWheelPanStep }, { DocArgs::kDy, io.MouseWheel * Navigation::kWheelPanStep } });
    }
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Middle) && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
        ui().actions.run(Actions::CanvasPan, { { DocArgs::kDx, io.MouseDelta.x }, { DocArgs::kDy, io.MouseDelta.y } });
    m_hasPointer = hovered || active;
    ui().tools.setSpringHand(m_hasPointer && !ui().owner.typing() && ui().input.isDown(Key::Space));
    if (m_hasPointer) ui().owner.claim(ActionScope::Canvas, InputOwner::Claim::Pointer);   // [ ] brush size, ... next frame

    // The rest is the tool's: ImGui decides whether the pointer is ours, SDL gives every sample.
    Tool& tool = ui().tools.active();
    m_pointer.clear();
    if (hovered || active || tool.busy())   // busy: a stroke still gets its release outside the canvas
        for (const PointerEvent& ev : ui().input.pointer)
            m_pointer.push_back({ ev, cam.screenToWorld({ ev.x - origin.x, ev.y - origin.y }) });
    tool.canvasInput({ ui().project, ui().actions, mouse, cam.screenToWorld(mouse), m_pointer, ui().input, hovered, active });
}

void CanvasPanel::updateOutlines()
{
    if (!m_ready) return;
    // Bounds of the nodes selected in the graph; with none selected, of the raster being edited.
    // Under the edit in progress: they follow a drag or a stroke as it goes.
    std::vector<BoundsI> outlined;
    const NodeSelection& selection = ui().project.selection();
    for (NodeId id : selection.ids()) outlined.push_back(m_preview->model().bounds(id));
    if (selection.empty() && ui().project.activeRaster()) outlined.push_back(m_preview->model().bounds(ui().project.activeRasterId()));
    std::erase_if(outlined, [](const BoundsI& b) { return b.empty(); });
    while (m_selectionBounds.size() < outlined.size()) {
        m_selectionBounds.push_back(m_scene.addItem<OutlineItem>());
        m_selectionBounds.back()->setZ(kZBounds);
    }
    for (size_t i = 0; i < m_selectionBounds.size(); ++i) {
        m_selectionBounds[i]->setVisible(i < outlined.size());
        if (i < outlined.size()) m_selectionBounds[i]->setPixelBounds(outlined[i]);
    }

    // The master's artboards: their frames, under the outlines.
    const Artboards& boards = ui().project.artboards();
    while (m_artboards.size() < boards.size()) {
        m_artboards.push_back(m_scene.addItem<ArtboardFrameItem>());
        m_artboards.back()->setZ(kZBounds - 1.0f);
    }
    for (size_t i = 0; i < m_artboards.size(); ++i) {
        m_artboards[i]->setVisible(i < boards.size());
        if (i < boards.size()) m_artboards[i]->set(boards[i]);
    }

    // A drag that doesn't reach the master (nothing on the canvas changes): outline the area it
    // affects, and for a move where it was.
    const EditPreview* pv = ui().project.preview();
    const Node* edited = pv && !pv->stroke() ? ui().project.find(pv->pixelsNode()) : nullptr;
    if (edited && (ui().project.isMaster(edited->id()) || ui().project.master().dependsOn(edited))) edited = nullptr;
    BoundsI from, to;
    if (edited) {
        to = m_preview->model().bounds(edited->id());
        if (pv->offsetOf(edited->id())) from = edited->bounds();
    }
    m_previewFrom->setVisible(!from.empty() && from != to);
    if (!from.empty()) m_previewFrom->setPixelBounds(from);
    m_previewTo->setVisible(!to.empty());
    if (!to.empty()) m_previewTo->setPixelBounds(to);
}

void CanvasPanel::drawHud(const Painter& draw, const ImVec2& origin)
{
    const RasterRootNode* active = ui().project.activeRaster();
    const bool shown = active && ui().project.master().dependsOn(active);
    char hud[256];
    std::snprintf(hud, sizeof hud, "%.0f%%   %s   painting: %s%s%s   level %d: %zu tiles / %zu atlas pages%s",
                  m_view.camera().zoom * 100.0f, ui().tools.active().name(),
                  active ? active->label().c_str() : "nothing (click a raster node)",
                  active && active->locked() ? " (locked)" : "", active && !shown ? " (not in Master)" : "",
                  m_output->drawLevel(), m_tiles->tileCount(), m_tiles->pageCount(), m_output->syncPending() ? " (syncing)" : "");
    const Style::HudStyle& style = Style::get().hud;
    const ImVec2 size = ImGui::CalcTextSize(hud);
    const ImVec2 pos(origin.x + style.margin.x, origin.y + m_view.height() - size.y - style.margin.y * 2.0f);
    draw.fillRect(ImVec2(pos.x - style.padding.x, pos.y - style.padding.y), ImVec2(pos.x + size.x + style.padding.x, pos.y + size.y + style.padding.y),
                  style.back, style.rounding);
    draw.text(pos, style.text, hud);
}

void CanvasPanel::renderGpu(WGPUCommandEncoder enc)
{
    if (!m_ready || !m_visible) return;
    m_view.render(enc, ui().canvas.budget(CanvasEngine::Work::Canvas));   // re-renders only when something changed
}
