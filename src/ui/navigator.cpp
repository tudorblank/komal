#include "ui/navigator.hpp"
#include "core/application.hpp"
#include "core/project.hpp"
#include "ui/panel-titles.hpp"
#include "ui/widgets.hpp"

#include <imgui_internal.h>   // ImRect

#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace {

constexpr float  kMapInset      = 6.0f;    // the picture inside the frame, points
constexpr float  kMinMapHeight  = 60.0f;   // points
constexpr float  kZoomStep      = 1.25f;   // per - / + click
constexpr float  kWheelZoomStep = 1.1f;    // per Ctrl+wheel notch (the canvas's)
constexpr float  kRotateStep    = 15.0f;   // degrees per click
constexpr float  kPi            = 3.14159265358979f;

constexpr ImU32 kMapEmpty    = IM_COL32( 62,  62,  62, 255);
constexpr ImU32 kMapViewport = IM_COL32(214,  72,  72, 255);
constexpr float kCheckerDark[3]  = { 52 / 255.0f, 52 / 255.0f, 52 / 255.0f };   // at the picture's corner
constexpr float kCheckerLight[3] = { 72 / 255.0f, 72 / 255.0f, 72 / 255.0f };

float toDegrees(float radians) { return radians * 180.0f / kPi; }
float toRadians(float degrees) { return degrees * kPi / 180.0f; }

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

void Navigator::init(Project& project, RasterTiles& tiles, const CanvasPipelines& pipelines)
{
    m_project = &project;
    m_ready = m_scene.init(pipelines) && m_view.init(m_scene);
    if (!m_ready) return;
    m_checker = m_scene.addItem<CheckerItem>();
    m_checker->setColors(kCheckerDark, kCheckerLight);
    m_checker->setZ(-1.0f);
    m_scene.addItem<RasterItem>(tiles);
}

void Navigator::shutdown()
{
    m_view.shutdown();
    m_scene.shutdown();
    m_checker = nullptr;
    m_ready = false;
    m_pictureShown = false;
}

void Navigator::draw(const CanvasNavigation& canvas, ActionRegistry& actions)
{
    m_pictureShown = false;
    if (!m_open) return;
    if (Widgets::beginPanel(PanelTitle::kNavigator, actions, "view.navigator", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (!canvas.camera) {
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
            drawMap(ImVec2(avail.x, mapH), canvas);
            ImGui::SetCursorPosY(top + mapH + m.panelPadding - inRow);
            drawControls(canvas);
        }
    }
    Widgets::endPanel();
}

void Navigator::renderGpu(WGPUCommandEncoder enc)
{
    if (!m_pictureShown) return;
    m_view.setHoldAllowed(!m_project->strokeActive());   // a stroke shows as it's painted, like on the canvas
    m_view.render(enc, kBuildBudgetMs);   // only when the tiles or the map changed
}

double Navigator::nextFrameIn() const
{
    return m_pictureShown && m_view.busy() ? 0.0 : Application::kNoFrame;
}

void Navigator::drawMap(ImVec2 size, const CanvasNavigation& canvas)
{
    CanvasCamera& cam = *canvas.camera;
    const ImRect map(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + size.x, ImGui::GetCursorScreenPos().y + size.y));
    ImGui::InvisibleButton("##map", size);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = ImGui::GetStyle().FrameRounding;
    dl->AddRectFilled(map.Min, map.Max, ImGui::GetColorU32(ImGuiCol_MenuBarBg), rounding);   // the theme's dark grey

    const ImGuiIO& io = ImGui::GetIO();
    const float inset = kMapInset * (ImGui::GetFontSize() / 15.0f);
    const float iw = std::max(1.0f, size.x - 2.0f * inset), ih = std::max(1.0f, size.y - 2.0f * inset);
    const BoundsI b = m_project->master().bounds();
    if (b.empty()) {
        const char* text = "Nothing on the canvas yet";
        const ImVec2 t = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2(map.Min.x + (size.x - t.x) * 0.5f, map.Min.y + (size.y - t.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
        return;
    }

    // The master's bounds, as large as the map allows, proportions kept, centred.
    const float bw = float(b.maxX - b.minX + 1), bh = float(b.maxY - b.minY + 1);
    const float k = std::min(iw / bw, ih / bh);   // map points per world pixel
    const ImVec2 offset(map.Min.x + inset + (iw - bw * k) * 0.5f - float(b.minX) * k,
                        map.Min.y + inset + (ih - bh * k) * 0.5f - float(b.minY) * k);
    const auto toMap   = [&](Vec2 w) { return ImVec2(offset.x + w.x * k, offset.y + w.y * k); };
    const auto toWorld = [&](ImVec2 s) { return Vec2{ (s.x - offset.x) / k, (s.y - offset.y) / k }; };
    const auto viewQuad = [&](ImVec2 (&q)[4]) {
        const Vec2 c[4] = { cam.screenToWorld({ 0.0f, 0.0f }), cam.screenToWorld({ canvas.viewW, 0.0f }),
                            cam.screenToWorld({ canvas.viewW, canvas.viewH }), cam.screenToWorld({ 0.0f, canvas.viewH }) };
        for (int i = 0; i < 4; ++i) q[i] = toMap(c[i]);
    };

    // Navigation first, so the outline is this frame's. Pressing the outline grabs it; elsewhere centres there.
    const Vec2 center = { canvas.viewW * 0.5f, canvas.viewH * 0.5f };
    if (hovered && io.KeyCtrl && io.MouseWheel != 0.0f) cam.zoomAt(center, std::pow(kWheelZoomStep, io.MouseWheel));
    if (ImGui::IsItemActivated()) {
        ImVec2 q[4];
        viewQuad(q);
        const Vec2 m = toWorld(io.MousePos), c = cam.screenToWorld(center);
        m_grab = insideQuad(q, io.MousePos) ? Vec2{ c.x - m.x, c.y - m.y } : Vec2{};
        m_dragging = true;
    }
    if (!active) m_dragging = false;
    if (m_dragging) {
        const Vec2 m = toWorld(io.MousePos);
        cam.centerOn(m.x + m_grab.x, m.y + m_grab.y, canvas.viewW, canvas.viewH);
    }

    // The picture: a view of the output's tiles over exactly the bounds, rendered in renderGpu().
    const ImVec2 c0 = toMap({ float(b.minX), float(b.minY) }), c1 = toMap({ float(b.maxX + 1), float(b.maxY + 1) });
    const ImVec2 fb = io.DisplayFramebufferScale;
    if (m_ready && m_view.resize(c1.x - c0.x, c1.y - c0.y, fb.x, fb.y)) {
        CanvasCamera& pc = m_view.camera();
        pc.zoom = k;
        pc.pan  = { -float(b.minX) * k, -float(b.minY) * k };   // the bounds' corner at the view's
        m_checker->setCellSize(kCheckerPixels / fb.x);
        m_pictureShown = true;
        dl->AddImage((ImTextureID)(uintptr_t)m_view.texture(), c0, c1);
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

void Navigator::drawControls(const CanvasNavigation& canvas)
{
    // Per setting: [-] slider [+] [value], the slider's figure saying which (a ramp: zoom; a wave:
    // rotation). Double-clicking a slider resets it.
    using namespace Widgets;
    CanvasCamera& cam = *canvas.camera;
    const Vec2 center = { canvas.viewW * 0.5f, canvas.viewH * 0.5f };
    const Metrics m = metrics();
    const float afterSlider = m.boxHeight + m.spacing + m.fieldWidth;   // the second button and the field

    // ---- zoom
    if (iconButton("##zoomOut", Icon::Minus)) cam.zoomAt(center, 1.0f / kZoomStep);
    sameRow();
    float percent = cam.zoom * 100.0f;
    if (figureSlider("##zoomSlider", percent, CanvasCamera::kMinZoom * 100.0f, CanvasCamera::kMaxZoom * 100.0f, stretchWidth(afterSlider),
                     SliderFigure::Ramp, true, 100.0f))
        cam.zoomAt(center, percent / 100.0f / cam.zoom);
    sameRow();
    if (iconButton("##zoomIn", Icon::Plus)) cam.zoomAt(center, kZoomStep);
    alignRight(m.fieldWidth);
    percent = cam.zoom * 100.0f;
    if (numberField("##zoomField", percent, CanvasCamera::kMinZoom * 100.0f, CanvasCamera::kMaxZoom * 100.0f, "%.0f%%"))
        cam.zoomAt(center, percent / 100.0f / cam.zoom);

    // ---- rotation: the slider has 0 in the middle (-180..180), the field reads 0..360
    const float degrees = toDegrees(cam.rotation);
    if (iconButton("##rotateLeft", Icon::RotateLeft)) cam.rotateAt(center, toRadians(degrees - kRotateStep));
    sameRow();
    float signedDegrees = degrees > 180.0f ? degrees - 360.0f : degrees;
    if (figureSlider("##rotationSlider", signedDegrees, -180.0f, 180.0f, stretchWidth(afterSlider), SliderFigure::Wave, false, 0.0f))
        cam.rotateAt(center, toRadians(signedDegrees));
    sameRow();
    if (iconButton("##rotateRight", Icon::RotateRight)) cam.rotateAt(center, toRadians(degrees + kRotateStep));
    alignRight(m.fieldWidth);
    float typed = toDegrees(cam.rotation);
    if (numberField("##rotationField", typed, -1e6f, 1e6f, "%.1f°"))
        cam.rotateAt(center, toRadians(typed));   // any angle: 360 is 0, -15 is 345
}
