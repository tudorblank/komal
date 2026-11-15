#include "ui/panels/canvas/canvas-panel.hpp"
#include "ui/panels/panel-window.hpp"
#include "ui/input-owner.hpp"
#include "project/project-actions.hpp"
#include "ui/render/painter.hpp"
#include "ui/style/ui-style.hpp"
#include "ui/panels/canvas/canvas-engine.hpp"
#include "ui/panels/canvas/imgui-texture.hpp"
#include "project/workspace.hpp"
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
    if (ui().projects.empty()) return ui().canvas.ready();   // a canvas is made as one is shown
    show(ui().projects.active().id());
    return m_canvas && m_canvas->ready;
}

CanvasPanel::Canvas* CanvasPanel::canvasOf(ProjectId id)
{
    for (const auto& c : m_canvases)
        if (c->project == id) return c.get();
    return nullptr;
}

void CanvasPanel::show(ProjectId id)
{
    m_canvas = canvasOf(id);
    if (m_canvas) return;
    // New: made from the engine's pictures of it (it's the active one now).
    auto made = std::make_unique<Canvas>();
    Canvas& c = *made;
    c.project  = id;
    c.workspace = &ui().workspace();
    c.preview   = &ui().canvas.preview();
    m_canvases.push_back(std::move(made));
    m_canvas = &c;
    if (!makeGpu(c)) return;

    // The scene: the workspace's output (unbounded), overlays on top.
    c.scene.addItem<CheckerItem>()->setZ(kZBackground);   // transparency, wherever nothing covers it
    c.output = c.scene.addItem<RasterItem>(ui().canvas.outputTiles());
    c.output->setZ(kZOutput);
    c.previewItem = c.scene.addItem<PreviewItem>(*c.preview, *c.output);
    c.previewItem->setZ(kZOutput);
    c.scene.addItem<OriginItem>()->setZ(kZOrigin);
    c.previewFrom = c.scene.addItem<OutlineItem>(OutlineItem::Kind::PreviewFrom);
    c.previewFrom->setZ(kZBounds);
    c.previewTo = c.scene.addItem<OutlineItem>(OutlineItem::Kind::PreviewTo);
    c.previewTo->setZ(kZBounds);

    c.events = c.workspace->changed.connect([this, &c](const WorkspaceEvent& e) { onWorkspaceEvent(c, e); });
    updateOutlines(c);
}

bool CanvasPanel::makeGpu(Canvas& c)
{
    c.ready = ui().canvas.ready() && c.scene.init(ui().canvas.pipelines()) && c.view.init(c.scene);
    if (c.ready) c.view.camera() = c.workspace->canvasCamera();
    return c.ready;
}

void CanvasPanel::projectsChanged(const ProjectsEvent& e)
{
    switch (e.type) {
    case ProjectsEvent::Type::Opened:
        break;   // its canvas is made once it's shown
    case ProjectsEvent::Type::Closing:
        if (m_canvas && m_canvas->project == e.id) m_canvas = nullptr;
        std::erase_if(m_canvases, [&](const auto& c) { return c->project == e.id; });
        break;
    case ProjectsEvent::Type::Activated:
        show(e.id);
        break;
    }
}

void CanvasPanel::onWorkspaceEvent(Canvas& c, const WorkspaceEvent& e)
{
    switch (e.type) {
    case WorkspaceEventType::CanvasViewChanged:
        c.view.camera() = c.workspace->canvasCamera();
        break;
    case WorkspaceEventType::PreviewChanged:
        updateOutlines(c);
        break;
    case WorkspaceEventType::NodeAdded:
    case WorkspaceEventType::NodeMoved:
        break;
    default:
        updateOutlines(c);
        break;
    }
}

void CanvasPanel::releaseGpu()
{
    for (const auto& c : m_canvases) {
        c->view.shutdown();
        c->scene.releaseGpu();
        c->ready = false;
    }
}

void CanvasPanel::restoreGpu()
{
    m_canvas = nullptr;
    std::erase_if(m_canvases, [](const auto& c) { return !c->output; });   // never made (no GPU then): made as they're shown
    for (const auto& c : m_canvases) makeGpu(*c);
    if (!ui().projects.empty()) show(ui().projects.active().id());
}

void CanvasPanel::shutdown()
{
    m_canvas = nullptr;
    m_canvases.clear();   // each one's view, scene and items, its connection
}

void CanvasPanel::draw()
{
    m_surface.hide();
    m_hasPointer = false;
    if (!isOpen()) {
        tellSize({});
        return;
    }

    const Widgets::PanelWindow window(*this, ui().actions, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse,
                                      Widgets::PanelWindow::Padding::None);
    if (window && m_canvas && m_canvas->ready) {
        CanvasView& view = m_canvas->view;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail  = ImGui::GetContentRegionAvail();
        const ImVec2 fb     = ImGui::GetIO().DisplayFramebufferScale;   // HiDPI: render at native pixels
        if (avail.x >= 1.0f && avail.y >= 1.0f && view.resize(avail.x, avail.y, fb.x, fb.y)) {
            m_surface.show(ImGui::GetCurrentWindowRead(), origin, avail);
            tellSize({ avail.x, avail.y });   // before input: what it frames is this frame's size

            // One item covers the canvas and owns the mouse while a button is held.
            ImGui::InvisibleButton("##canvas", avail, ImGuiButtonFlags_MouseButtonLeft |
                                                      ImGuiButtonFlags_MouseButtonRight |
                                                      ImGuiButtonFlags_MouseButtonMiddle);
            handleInput(origin);

            const Painter draw = Painter::window();
            draw.image(imguiTexture(view.texture()), origin, ImVec2(origin.x + avail.x, origin.y + avail.y));
            view.drawOverlays(draw, origin.x, origin.y);
            drawHud(draw, origin);
        }
    }
    if (!m_surface.shown()) tellSize({});   // collapsed, tabbed away
}

void CanvasPanel::tellSize(Vec2 size)
{
    if (size != ui().workspace().canvasViewport()) ui().actions.run(Actions::CanvasViewport, { { ProjectArgs::kWidth, size.x }, { ProjectArgs::kHeight, size.y } });
}

bool CanvasPanel::dropTarget(const ImGuiWindow* window, ImVec2 screen, Vec2& world) const
{
    if (!m_canvas || !m_surface.contains(window, screen)) return false;
    const ImVec2 p = m_surface.local(screen);
    world = m_canvas->view.camera().screenToWorld({ p.x, p.y });
    return true;
}

void CanvasPanel::handleInput(const ImVec2& origin)
{
    ImGuiIO& io = ImGui::GetIO();
    const CanvasCamera& cam = m_canvas->view.camera();
    const Vec2 mouse = { io.MousePos.x - origin.x, io.MousePos.y - origin.y };
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();

    // Navigation that works whatever the tool.
    const InputFrame& input = ui().input;
    if (hovered && input.wheeled()) {
        if (input.mods & ModPrimary)
            ui().actions.run(Actions::CanvasZoom, { { ProjectArgs::kFactor, std::pow(Navigation::kWheelZoomStep, input.wheelY) }, { ProjectArgs::kX, mouse.x }, { ProjectArgs::kY, mouse.y } });
        else
            ui().actions.run(Actions::CanvasPan, { { ProjectArgs::kDx, input.wheelX * Navigation::kWheelPanStep }, { ProjectArgs::kDy, input.wheelY * Navigation::kWheelPanStep } });
    }
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Middle) && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
        ui().actions.run(Actions::CanvasPan, { { ProjectArgs::kDx, io.MouseDelta.x }, { ProjectArgs::kDy, io.MouseDelta.y } });
    m_hasPointer = hovered || active;
    ui().tools.setSpringHand(m_hasPointer && !ui().owner.typing() && ui().actions.held(Actions::ToolHandHold, input));
    if (m_hasPointer) ui().owner.claim(ActionScope::Canvas, InputOwner::Claim::Pointer);   // [ ] brush size, ... next frame

    // The rest is the tool's: ImGui decides whether the pointer is ours, SDL gives every sample.
    Tool& tool = ui().tools.active();
    m_pointer.clear();
    if (hovered || active || tool.busy())   // busy: a stroke still gets its release outside the canvas
        for (const PointerEvent& ev : input.pointer)
            m_pointer.push_back({ ev, cam.screenToWorld({ ev.x - origin.x, ev.y - origin.y }) });
    tool.canvasInput({ ui().workspace(), ui().actions, m_pointer, input, active });
}

void CanvasPanel::updateOutlines(Canvas& c)
{
    if (!c.ready) return;
    const Workspace& workspace = *c.workspace;
    // Bounds of the nodes selected in the graph; with none selected, of the raster being edited.
    // Under the edit in progress: they follow a drag or a stroke as it goes.
    std::vector<BoundsI> outlined;
    const NodeSelection& selection = workspace.selection();
    for (NodeId id : selection.ids()) outlined.push_back(c.preview->model().bounds(id));
    if (selection.empty() && workspace.activeRaster()) outlined.push_back(c.preview->model().bounds(workspace.activeRasterId()));
    std::erase_if(outlined, [](const BoundsI& b) { return b.empty(); });
    while (c.selectionBounds.size() < outlined.size()) {
        c.selectionBounds.push_back(c.scene.addItem<OutlineItem>());
        c.selectionBounds.back()->setZ(kZBounds);
    }
    for (size_t i = 0; i < c.selectionBounds.size(); ++i) {
        c.selectionBounds[i]->setVisible(i < outlined.size());
        if (i < outlined.size()) c.selectionBounds[i]->setPixelBounds(outlined[i]);
    }

    // The master's artboards: their frames, under the outlines.
    const Artboards& boards = workspace.artboards();
    while (c.artboards.size() < boards.size()) {
        c.artboards.push_back(c.scene.addItem<ArtboardFrameItem>());
        c.artboards.back()->setZ(kZBounds - 1.0f);
    }
    for (size_t i = 0; i < c.artboards.size(); ++i) {
        c.artboards[i]->setVisible(i < boards.size());
        if (i < boards.size()) c.artboards[i]->set(boards[i]);
    }

    // A drag that doesn't reach the master (nothing on the canvas changes): outline the area it
    // affects, and for a move where it was.
    const EditPreview* pv = workspace.preview();
    const Node* edited = pv && !pv->stroke() ? workspace.find(pv->pixelsNode()) : nullptr;
    if (edited && (workspace.isMaster(edited->id()) || workspace.master().dependsOn(edited))) edited = nullptr;
    BoundsI from, to;
    if (edited) {
        to = c.preview->model().bounds(edited->id());
        if (pv->offsetOf(edited->id())) from = edited->bounds();
    }
    c.previewFrom->setVisible(!from.empty() && from != to);
    if (!from.empty()) c.previewFrom->setPixelBounds(from);
    c.previewTo->setVisible(!to.empty());
    if (!to.empty()) c.previewTo->setPixelBounds(to);
}

void CanvasPanel::drawHud(const Painter& draw, const ImVec2& origin)
{
    const RasterRootNode* active = ui().workspace().activeRaster();
    const bool shown = active && ui().workspace().master().dependsOn(active);
    char hud[256];
    std::snprintf(hud, sizeof hud, "%.0f%%   %s   painting: %s%s%s   level %d: %zu tiles / %zu atlas pages%s",
                  m_canvas->view.camera().zoom * 100.0f, ui().tools.active().name(),
                  active ? active->label().c_str() : "nothing (click a raster node)",
                  active && active->locked() ? " (locked)" : "", active && !shown ? " (not in Master)" : "",
                  m_canvas->output->drawLevel(), ui().canvas.tiles().tileCount(), ui().canvas.tiles().pageCount(), m_canvas->output->syncPending() ? " (syncing)" : "");
    const Style::HudStyle& style = Style::get().hud;
    const ImVec2 size = ImGui::CalcTextSize(hud);
    const ImVec2 pos(origin.x + style.margin.x, origin.y + m_canvas->view.height() - size.y - style.margin.y * 2.0f);
    draw.fillRect(ImVec2(pos.x - style.padding.x, pos.y - style.padding.y), ImVec2(pos.x + size.x + style.padding.x, pos.y + size.y + style.padding.y),
                  style.back, style.rounding);
    draw.text(pos, style.text, hud);
}

void CanvasPanel::renderGpu(WGPUCommandEncoder enc)
{
    if (!m_canvas || !m_canvas->ready || !m_surface.shown()) return;
    m_canvas->view.render(enc, ui().canvas.budget(CanvasEngine::Work::Canvas));   // re-renders only when something changed
}
