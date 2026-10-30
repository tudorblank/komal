#include "ui/navigator.hpp"
#include "base/math.hpp"
#include "canvas/canvas-engine.hpp"
#include "command/input.hpp"
#include "ui/navigation.hpp"
#include "base/frames.hpp"
#include "base/palette.hpp"
#include "document/project.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui_internal.h>   // ImRect

#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace {

constexpr float  kMapInset      = 6.0f;    // the picture inside the frame, points
constexpr float  kMinMapHeight  = 60.0f;   // points

constexpr ImU32 kMapEmpty    = Palette::kEmptyArea.packed();
constexpr ImU32 kMapViewport = Palette::kViewOutline.packed();

// A convex polygon: a quad clipped by a rectangle has at most 8 corners (one more per edge).
struct Poly {
    static constexpr int kMax = 8;
    ImVec2 p[kMax];
    int    n = 0;
    void add(ImVec2 v) { if (n < kMax) p[n++] = v; }
};

// `poly` clipped to `r` (Sutherland-Hodgman, one edge of the rectangle at a time).
Poly clipToRect(Poly poly, const ImRect& r)
{
    const auto clipEdge = [&](auto inside, auto cross) {
        Poly out;
        for (int i = 0; i < poly.n; ++i) {
            const ImVec2 a = poly.p[i], b = poly.p[(i + 1) % poly.n];
            const bool ia = inside(a), ib = inside(b);
            if (ia) out.add(a);
            if (ia != ib) out.add(cross(a, b));
        }
        poly = out;
    };
    const auto atX = [](ImVec2 a, ImVec2 b, float x) { return ImVec2(x, a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x)); };
    const auto atY = [](ImVec2 a, ImVec2 b, float y) { return ImVec2(a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y), y); };
    clipEdge([&](ImVec2 p) { return p.x >= r.Min.x; }, [&](ImVec2 a, ImVec2 b) { return atX(a, b, r.Min.x); });
    clipEdge([&](ImVec2 p) { return p.x <= r.Max.x; }, [&](ImVec2 a, ImVec2 b) { return atX(a, b, r.Max.x); });
    clipEdge([&](ImVec2 p) { return p.y >= r.Min.y; }, [&](ImVec2 a, ImVec2 b) { return atY(a, b, r.Min.y); });
    clipEdge([&](ImVec2 p) { return p.y <= r.Max.y; }, [&](ImVec2 a, ImVec2 b) { return atY(a, b, r.Max.y); });
    return poly;
}

bool insideQuad(const ImVec2 (&q)[4], ImVec2 p)
{
    bool pos = false, neg = false;
    for (int i = 0; i < 4; ++i) {
        const ImVec2 a = q[i], b = q[(i + 1) % 4];
        const float c = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        pos |= c > 0.0f;
        neg |= c < 0.0f;
    }
    return !(pos && neg);
}

}

PanelInfo Navigator::info() const { return { Actions::ViewNavigator, "Navigator", DockSlot::RightTop }; }

bool Navigator::init()
{
    m_preview   = &ui().canvas.preview();
    m_pipelines = &ui().canvas.pipelines();
    m_ready = ui().canvas.ready() && m_scene.init(ui().canvas.pipelines()) && m_view.init(m_scene);
    if (!m_ready) return false;
    m_checker = m_scene.addItem<CheckerItem>();
    m_checker->setColors(Palette::kPictureCheckerA, Palette::kPictureCheckerB);
    m_checker->setZ(-1.0f);
    m_scene.addItem<RasterItem>(ui().canvas.outputTiles());
    return true;
}

void Navigator::shutdown()
{
    m_previewPicture.release();
    m_view.shutdown();
    m_scene.shutdown();
    m_checker = nullptr;
    m_ready = false;
    m_pictureShown = false;
}

void Navigator::draw()
{
    m_pictureShown = false;
    if (!isOpen()) return;
    if (const Widgets::PanelWindow window(*this, ui().actions, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse); window) {
        const Vec2 viewport = ui().project.canvasViewport();
        if (viewport.x <= 0.0f || viewport.y <= 0.0f) {
            ImGui::TextDisabled("The canvas is hidden.");
        } else {
            // One spacing around everything: the panel's padding, also between the map and the
            // controls; the two rows as close as rows are in every panel. Measured box to box (a
            // row is a little taller than the boxes in it).
            const Widgets::Metrics m = Widgets::metrics();
            const float inRow  = (m.rowHeight - m.boxHeight) * 0.5f;                 // a row's box, from its top
            const float rowGap = m.rowHeight - m.boxHeight + ImGui::GetStyle().ItemSpacing.y;
            const float controls = m.panelPadding + 2.0f * m.boxHeight + rowGap;     // map bottom to the last box's bottom
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float top = ImGui::GetCursorPosY();
            const float mapH = std::max(kMinMapHeight, std::floor(avail.y - controls));
            drawMap(ImVec2(avail.x, mapH));
            ImGui::SetCursorPosY(top + mapH + m.panelPadding - inRow);
            drawControls();
        }
    }
}

void Navigator::renderGpu(WGPUCommandEncoder enc)
{
    if (!m_pictureShown) return;
    m_previewWaiting = false;
    if (m_preview->active() && m_previewW > 0) {
        const auto r = m_previewPicture.render(*m_preview, ui().project.master().id(), m_previewW, m_previewH, *m_pipelines, enc,
                                               ui().canvas.budget(CanvasEngine::Work::Navigator));
        m_previewWaiting = r == PreviewPicture::Result::Waiting;
    }
    m_view.render(enc, ui().canvas.budget(CanvasEngine::Work::Navigator));   // only when the tiles or the map changed
}

double Navigator::nextFrameIn() const
{
    return m_pictureShown && (m_view.busy() || m_previewWaiting || m_handoff) ? 0.0 : kNoFrame;
}

bool Navigator::showsPreview()
{
    if (m_preview->active() && m_preview->plan().reaches(ui().project.master().id())) {
        m_handoff = m_previewPicture.hasPicture();
        m_handoffFrames = 0;
        return m_handoff;
    }
    // Over: kept until the view has had a frame to see the result, and has caught up with it.
    if (m_handoff && m_handoffFrames++ > 0 && !m_view.busy()) m_handoff = false;
    if (!m_handoff) m_previewPicture.release();
    return m_handoff;
}

void Navigator::zoomBy(float factor)
{
    ui().actions.run(Actions::CanvasZoom, { { "factor", factor } });
}

void Navigator::rotateTo(float degrees)
{
    ui().actions.run(Actions::CanvasRotate, { { "degrees", degrees } });
}

void Navigator::drawMap(ImVec2 size)
{
    const CanvasCamera& cam = ui().project.canvasCamera();
    const Vec2 viewport = ui().project.canvasViewport();
    const ImRect map(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + size.x, ImGui::GetCursorScreenPos().y + size.y));
    ImGui::InvisibleButton("##map", size);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = ImGui::GetStyle().FrameRounding;
    dl->AddRectFilled(map.Min, map.Max, ImGui::GetColorU32(ImGuiCol_MenuBarBg), rounding);   // the theme's dark grey

    const ImGuiIO& io = ImGui::GetIO();
    const float inset = kMapInset * (ImGui::GetFontSize() / 15.0f);
    const float iw = std::max(1.0f, size.x - 2.0f * inset), ih = std::max(1.0f, size.y - 2.0f * inset);
    // Under the edit: the picture renders this frame fitted to what the master covers now; handing
    // back after it, the picture shows what it last fitted.
    const bool preview = showsPreview();
    const NodeId master = ui().project.master().id();
    const bool live = m_preview->active() && m_preview->plan().reaches(master);
    const BoundsI b = !preview ? ui().project.master().bounds() : live ? m_preview->bounds(master) : m_previewPicture.bounds();
    if (b.empty()) {
        const char* text = "Nothing on the canvas yet";
        const ImVec2 t = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2(map.Min.x + (size.x - t.x) * 0.5f, map.Min.y + (size.y - t.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
        return;
    }

    // The master's bounds, as large as the map allows, proportions kept, centred.
    const float bw = float(b.maxX - b.minX + 1), bh = float(b.maxY - b.minY + 1);
    const float k = fitScale(b, iw, ih);   // map points per world pixel
    const ImVec2 offset(map.Min.x + inset + (iw - bw * k) * 0.5f - float(b.minX) * k,
                        map.Min.y + inset + (ih - bh * k) * 0.5f - float(b.minY) * k);
    const auto toMap   = [&](Vec2 w) { return ImVec2(offset.x + w.x * k, offset.y + w.y * k); };
    const auto toWorld = [&](ImVec2 s) { return Vec2{ (s.x - offset.x) / k, (s.y - offset.y) / k }; };
    const auto viewQuad = [&](ImVec2 (&q)[4]) {
        const Vec2 c[4] = { cam.screenToWorld({ 0.0f, 0.0f }), cam.screenToWorld({ viewport.x, 0.0f }),
                            cam.screenToWorld({ viewport.x, viewport.y }), cam.screenToWorld({ 0.0f, viewport.y }) };
        for (int i = 0; i < 4; ++i) q[i] = toMap(c[i]);
    };

    // Navigation first, so the outline is this frame's (the camera is the project's: an action
    // changes it at once). Pressing the outline grabs it; elsewhere centres there.
    const Vec2 center = { viewport.x * 0.5f, viewport.y * 0.5f };
    if (hovered && (ui().input.mods & ModPrimary) && io.MouseWheel != 0.0f) zoomBy(std::pow(Navigation::kWheelZoomStep, io.MouseWheel));
    if (ImGui::IsItemActivated()) {
        ImVec2 q[4];
        viewQuad(q);
        const Vec2 m = toWorld(io.MousePos), c = cam.screenToWorld(center);
        m_grab = insideQuad(q, io.MousePos) ? Vec2{ c.x - m.x, c.y - m.y } : Vec2{};
        m_dragging = true;
    }
    if (!active) m_dragging = false;
    if (m_dragging) {
        const Vec2 m = toWorld(io.MousePos), to = { m.x + m_grab.x, m.y + m_grab.y };
        if (to != cam.screenToWorld(center)) ui().actions.run(Actions::CanvasCenterOn, { { "x", to.x }, { "y", to.y } });
    }

    // The picture: a view of the output's tiles over exactly the bounds, rendered in renderGpu().
    const ImVec2 c0 = toMap({ float(b.minX), float(b.minY) }), c1 = toMap({ float(b.maxX + 1), float(b.maxY + 1) });
    const ImVec2 fb = io.DisplayFramebufferScale;
    m_previewW = uint32_t(std::lround(iw * fb.x));   // the preview's picture fits there like the view does
    m_previewH = uint32_t(std::lround(ih * fb.y));
    if (m_ready && m_view.resize(c1.x - c0.x, c1.y - c0.y, fb.x, fb.y)) {
        CanvasCamera& pc = m_view.camera();
        pc.zoom = k;
        pc.pan  = { -float(b.minX) * k, -float(b.minY) * k };   // the bounds' corner at the view's
        m_checker->setCellSize(Palette::kPictureChecker / fb.x);
        m_pictureShown = true;
        if (preview) {
            const GpuTexture& t = m_previewPicture.texture();
            const ImVec2 uv1(float(m_previewPicture.width()) / float(t.width()), float(m_previewPicture.height()) / float(t.height()));
            dl->AddImage((ImTextureID)(uintptr_t)t.view(), c0, c1, ImVec2(0.0f, 0.0f), uv1);
        } else {
            dl->AddImage((ImTextureID)(uintptr_t)m_view.texture(), c0, c1);
        }
    } else {
        dl->AddRectFilled(c0, c1, kMapEmpty);   // too thin to render
    }

    // What the canvas shows, clamped to the map: a view bigger than the picture hugs the frame.
    ImVec2 q[4];
    viewQuad(q);
    const ImRect inner(ImVec2(map.Min.x + 1.0f, map.Min.y + 1.0f), ImVec2(map.Max.x - 1.0f, map.Max.y - 1.0f));
    const auto clamped = [&](ImVec2 p) { return ImVec2(std::clamp(p.x, inner.Min.x, inner.Max.x), std::clamp(p.y, inner.Min.y, inner.Max.y)); };
    bool drawn = false;
    if (cam.rotation == 0.0f) {
        const ImVec2 a = clamped(q[0]), z = clamped(q[2]);
        if (z.x > a.x && z.y > a.y) { dl->AddRect(a, z, kMapViewport, rounding, 0, 1.5f); drawn = true; }
    } else {
        Poly quad;
        for (const ImVec2& p : q) quad.add(p);
        const Poly poly = clipToRect(quad, inner);
        if (poly.n >= 3) { dl->AddPolyline(poly.p, poly.n, kMapViewport, ImDrawFlags_Closed, 1.5f); drawn = true; }
    }
    if (!drawn) {
        // Entirely off the map: flattened against the edge it went past, it's a line as long as
        // the view's height (left / right) or width (above / below); past a corner, a dot.
        ImRect bb(q[0], q[0]);
        for (const ImVec2& p : q) bb.Add(p);
        const ImVec2 a = clamped(bb.Min), z = clamped(bb.Max);
        if (z.x - a.x < 1.0f && z.y - a.y < 1.0f) dl->AddCircleFilled(a, 2.5f * Widgets::metrics().unit, kMapViewport);
        else                                      dl->AddLine(a, z, kMapViewport, 2.0f);
    }
}

void Navigator::drawControls()
{
    // Per setting: [-] slider [+] [value], the slider's figure saying which (a ramp: zoom; a wave:
    // rotation). Double-clicking a slider resets it.
    using namespace Widgets;
    const CanvasCamera& cam = ui().project.canvasCamera();
    const Metrics m = metrics();
    const float afterSlider = m.boxHeight + m.spacing + m.fieldWidth;   // the second button and the field

    // ---- zoom
    if (iconButton("##zoomOut", Icon::Minus)) zoomBy(1.0f / Navigation::kZoomButtonStep);
    sameRow();
    float percent = cam.zoom * 100.0f;
    if (figureSlider("##zoomSlider", percent, CanvasCamera::kMinZoom * 100.0f, CanvasCamera::kMaxZoom * 100.0f, stretchWidth(afterSlider),
                     SliderFigure::Ramp, true, 100.0f))
        zoomBy(percent / 100.0f / cam.zoom);
    sameRow();
    if (iconButton("##zoomIn", Icon::Plus)) zoomBy(Navigation::kZoomButtonStep);
    alignRight(m.fieldWidth);
    percent = cam.zoom * 100.0f;
    if (numberField("##zoomField", percent, CanvasCamera::kMinZoom * 100.0f, CanvasCamera::kMaxZoom * 100.0f, "%.0f%%"))
        zoomBy(percent / 100.0f / cam.zoom);

    // ---- rotation: the slider has 0 in the middle (-180..180), the field reads 0..360
    const float degrees = toDegrees(cam.rotation);
    if (iconButton("##rotateLeft", Icon::RotateLeft)) rotateTo(degrees - Navigation::kRotateStep);
    sameRow();
    float signedDegrees = degrees > 180.0f ? degrees - 360.0f : degrees;
    // -180 and 180 are the same angle: the slider stays at the end it was taken to.
    if (std::fabs(signedDegrees - 180.0f) < 1e-3f && m_rotationShown < 0.0f) signedDegrees = -180.0f;
    if (figureSlider("##rotationSlider", signedDegrees, -180.0f, 180.0f, stretchWidth(afterSlider), SliderFigure::Wave, false, 0.0f))
        rotateTo(signedDegrees);
    m_rotationShown = signedDegrees;
    sameRow();
    if (iconButton("##rotateRight", Icon::RotateRight)) rotateTo(degrees + Navigation::kRotateStep);
    alignRight(m.fieldWidth);
    float typed = toDegrees(cam.rotation);
    if (numberField("##rotationField", typed, -1e6f, 1e6f, "%.1f°"))
        rotateTo(typed);   // any angle: 360 is 0, -15 is 345
}
