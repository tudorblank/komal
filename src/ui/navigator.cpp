#include "ui/navigator.hpp"
#include "core/project.hpp"
#include "gfx/device.hpp"
#include "node/node-picture.hpp"
#include "ui/widgets.hpp"
#include "ui/tools/tool.hpp"

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

float toDegrees(float radians) { return radians * 180.0f / kPi; }
float toRadians(float degrees) { return degrees * kPi / 180.0f; }

// `poly` clipped to `r` (Sutherland-Hodgman, one edge of the rectangle at a time).
std::vector<ImVec2> clipToRect(std::vector<ImVec2> poly, const ImRect& r)
{
    const auto clipEdge = [&](auto inside, auto cross) {
        std::vector<ImVec2> out;
        for (size_t i = 0; i < poly.size(); ++i) {
            const ImVec2 a = poly[i], b = poly[(i + 1) % poly.size()];
            const bool ia = inside(a), ib = inside(b);
            if (ia) out.push_back(a);
            if (ia != ib) out.push_back(cross(a, b));
        }
        poly = std::move(out);
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

void Navigator::init(GpuDevice& gpu, Project& project)
{
    m_project = &project;
    m_device  = gpu.device();
    m_queue   = gpu.queue();

    WGPUTextureDescriptor td{};
    td.label         = sv("Navigator Picture");
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = { (uint32_t)kMaxPicture, (uint32_t)kMaxPicture, 1 };
    td.format        = WGPUTextureFormat_RGBA8Unorm;
    td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.mipLevelCount = 1;
    td.sampleCount   = 1;
    m_texture = wgpuDeviceCreateTexture(m_device, &td);
    if (m_texture) m_view = wgpuTextureCreateView(m_texture, nullptr);
}

void Navigator::shutdown()
{
    if (m_view)    { wgpuTextureViewRelease(m_view); m_view = nullptr; }
    if (m_texture) { wgpuTextureRelease(m_texture);  m_texture = nullptr; }
    m_picW = m_picH = 0;
}

void Navigator::draw(bool* open, CanvasNavigation& canvas)
{
    m_pending = false;
    if (Widgets::beginPanel("Navigator", open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (!canvas.camera) {
            ImGui::TextDisabled("The canvas is hidden.");
        } else {
            // The map takes what the controls below it leave: a gap, then two headed rows.
            const float below = Widgets::stackHeight(2, 2, 1) + ImGui::GetStyle().ItemSpacing.y;
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            drawMap(ImVec2(avail.x, std::max(kMinMapHeight, avail.y - below)), canvas);
            Widgets::sectionGap();
            drawControls(canvas);
        }
    }
    Widgets::endPanel();
}

void Navigator::drawMap(ImVec2 size, CanvasNavigation& canvas)
{
    CanvasCamera& cam = *canvas.camera;
    const ImRect map(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + size.x, ImGui::GetCursorScreenPos().y + size.y));
    ImGui::InvisibleButton("##map", size);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = ImGui::GetStyle().FrameRounding;
    dl->AddRectFilled(map.Min, map.Max, ImGui::GetColorU32(ImGuiCol_MenuBarBg), rounding);   // the theme's dark grey

    // Laid out from the picture's own bounds: mid-edit it never stretches, and re-fits once settled.
    const ImGuiIO& io = ImGui::GetIO();
    const float inset = kMapInset * (ImGui::GetFontSize() / 15.0f);
    const float iw = std::max(1.0f, size.x - 2.0f * inset), ih = std::max(1.0f, size.y - 2.0f * inset);
    updatePicture(m_project->master().bounds(), iw * io.DisplayFramebufferScale.x, ih * io.DisplayFramebufferScale.y);
    const BoundsI b = m_picW > 0 ? m_picBounds : m_project->master().bounds();
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

    // The picture.
    const ImVec2 c0 = toMap({ float(b.minX), float(b.minY) }), c1 = toMap({ float(b.maxX + 1), float(b.maxY + 1) });
    if (m_picW > 0 && m_picH > 0)
        dl->AddImage((ImTextureID)(uintptr_t)m_view, c0, c1, ImVec2(0.0f, 0.0f), ImVec2(float(m_picW) / kMaxPicture, float(m_picH) / kMaxPicture));
    else
        dl->AddRectFilled(c0, c1, kMapEmpty);   // until its first render

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
        const std::vector<ImVec2> poly = clipToRect({ q[0], q[1], q[2], q[3] }, inner);
        if (poly.size() >= 3) { dl->AddPolyline(poly.data(), int(poly.size()), kMapViewport, ImDrawFlags_Closed, 1.5f); drawn = true; }
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

void Navigator::updatePicture(const BoundsI& bounds, float areaW, float areaH)
{
    Node& master = m_project->master();
    const bool stale = master.revision() != m_picRevision;
    if (m_project->strokeActive() || !m_texture) return;   // caught up once the stroke ends (it wakes the app)
    if (bounds.empty()) {   // nothing left to show
        m_picW = m_picH = 0;
        m_picBounds   = {};
        m_picRevision = master.revision();
        return;
    }

    // The bounds fitted into the area, proportions kept; only the largest side is capped.
    const float bw = float(bounds.maxX - bounds.minX + 1), bh = float(bounds.maxY - bounds.minY + 1);
    const float fit = std::min({ areaW / bw, areaH / bh, float(kMaxPicture) / std::max(bw, bh) });
    const int width  = std::clamp(int(std::lround(bw * fit)), 1, kMaxPicture);
    const int height = std::clamp(int(std::lround(bh * fit)), 1, kMaxPicture);

    const double now = ImGui::GetTime();
    if (width != m_wantW || height != m_wantH) { m_wantW = width; m_wantH = height; m_wantSince = now; }
    const bool resized = width != m_picW || height != m_picH;
    if (!stale && !resized) return;
    m_pending = true;
    if (m_picW > 0 && resized && !stale && now - m_wantSince < kSettle) return;   // mid-resize: stretch the old one
    if (m_picW > 0 && stale && now - m_renderedAt < kMinInterval) return;
    renderPicture(master, width, height);
    m_picBounds   = bounds;
    m_picRevision = master.revision();
    m_renderedAt  = now;
    m_pending     = false;
}

void Navigator::renderPicture(Node& n, int width, int height)
{
    m_pixels.resize(size_t(width) * height);
    renderNodePicture(n, n.bounds(), width, height, m_pixels.data(), width, m_render);

    WGPUTexelCopyTextureInfo dst{};
    dst.texture = m_texture;
    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow  = uint32_t(width) * sizeof(RGBA);
    layout.rowsPerImage = uint32_t(height);
    const WGPUExtent3D extent = { uint32_t(width), uint32_t(height), 1 };
    wgpuQueueWriteTexture(m_queue, &dst, m_pixels.data(), m_pixels.size() * sizeof(RGBA), &layout, &extent);
    m_picW = width;
    m_picH = height;
}

void Navigator::drawControls(CanvasNavigation& canvas)
{
    // Per setting: its name, then [-] slider [+] [value]. Double-clicking a slider resets it.
    using namespace Widgets;
    CanvasCamera& cam = *canvas.camera;
    const Vec2 center = { canvas.viewW * 0.5f, canvas.viewH * 0.5f };
    const Metrics m = metrics();
    const float afterSlider = m.boxHeight + m.spacing + m.fieldWidth;   // the second button and the field

    // ---- zoom
    heading("Zoom");
    if (iconButton("##zoomOut", Icon::Minus)) cam.zoomAt(center, 1.0f / kZoomStep);
    sameRow();
    float percent = cam.zoom * 100.0f;
    if (slider("##zoomSlider", percent, CanvasCamera::kMinZoom * 100.0f, CanvasCamera::kMaxZoom * 100.0f, stretchWidth(afterSlider), true, 100.0f))
        cam.zoomAt(center, percent / 100.0f / cam.zoom);
    sameRow();
    if (iconButton("##zoomIn", Icon::Plus)) cam.zoomAt(center, kZoomStep);
    alignRight(m.fieldWidth);
    percent = cam.zoom * 100.0f;
    if (numberField("##zoomField", percent, CanvasCamera::kMinZoom * 100.0f, CanvasCamera::kMaxZoom * 100.0f, "%.0f%%"))
        cam.zoomAt(center, percent / 100.0f / cam.zoom);

    // ---- rotation: the slider has 0 in the middle (-180..180), the field reads 0..360
    heading("Rotation");
    const float degrees = toDegrees(cam.rotation);
    if (iconButton("##rotateLeft", Icon::RotateLeft)) cam.rotateAt(center, toRadians(degrees - kRotateStep));
    sameRow();
    float signedDegrees = degrees > 180.0f ? degrees - 360.0f : degrees;
    if (slider("##rotationSlider", signedDegrees, -180.0f, 180.0f, stretchWidth(afterSlider), false, 0.0f))
        cam.rotateAt(center, toRadians(signedDegrees));
    sameRow();
    if (iconButton("##rotateRight", Icon::RotateRight)) cam.rotateAt(center, toRadians(degrees + kRotateStep));
    alignRight(m.fieldWidth);
    float typed = toDegrees(cam.rotation);
    if (numberField("##rotationField", typed, -1e6f, 1e6f, "%.1f°"))
        cam.rotateAt(center, toRadians(typed));   // any angle: 360 is 0, -15 is 345
}
