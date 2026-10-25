#include "node-painter.h"
#include "ui/graph-view/graph-style.h"
#include "ui/graph-view/node-thumbnails.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

using namespace GraphStyle;

namespace {

constexpr float kPi = 3.14159265358979f;

// Font sizes are baked in steps that grow with the size, rounded up so text is never
// rendered from a smaller bake than the screen needs.
float quantizedSize(float px)
{
    const float step = px < 12.0f ? 0.5f : px < 24.0f ? 1.0f : px < 48.0f ? 2.0f : 4.0f;
    return std::ceil(px / step - 0.01f) * step;
}

void textCentered(const GraphPaint& g, const ImRect& r, float size, ImU32 color, const char* text)
{
    const float w = textWidth(size, text);
    drawText(g, ImVec2(r.GetCenter().x - w * 0.5f, r.GetCenter().y - size * 0.5f), size, color, text);
}

// Text vertically centred on a row, starting at x.
void textInRow(const GraphPaint& g, float x, const ImRect& row, float size, ImU32 color, const char* text)
{
    drawText(g, ImVec2(x, row.GetCenter().y - size * 0.55f), size, color, text);
}

void drawLockIcon(const GraphPaint& g, const ImRect& r, bool locked, bool hovered)
{
    const ImU32 col = locked ? kIconOn : hovered ? kTextColor : kIconOff;
    const ImVec2 a = g.at(r.Min), b = g.at(r.Max);
    const float w = b.x - a.x, h = b.y - a.y;
    const float bodyTop = a.y + h * 0.45f;
    g.dl->AddRectFilled(ImVec2(a.x + w * 0.12f, bodyTop), ImVec2(b.x - w * 0.12f, b.y), col, w * 0.12f);
    // Shackle: closed sits in the body, open is lifted and swung.
    const float rr = w * 0.26f, lift = locked ? 0.0f : h * 0.12f;
    const ImVec2 c(a.x + w * 0.5f, bodyTop - lift);
    g.dl->PathArcTo(c, rr, kPi, locked ? 2.0f * kPi : 1.75f * kPi, 12);
    g.dl->PathStroke(col, 0, std::max(1.0f, w * 0.13f));
    if (locked) g.dl->AddLine(ImVec2(c.x - rr, c.y), ImVec2(c.x - rr, bodyTop), col, std::max(1.0f, w * 0.13f));
}

// Lock transparency (Photoshop's checkerboard): a 3x3 checker, five squares filled.
void drawAlphaLockIcon(const GraphPaint& g, const ImRect& r, bool locked, bool hovered)
{
    const ImU32 col = locked ? kIconOn : hovered ? kTextColor : kIconOff;
    const ImVec2 a = g.at(r.Min), b = g.at(r.Max);
    const float inset = (b.x - a.x) * 0.1f;
    const float cell = (b.x - a.x - 2.0f * inset) / 3.0f;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            if ((x + y) % 2 == 0) {
                const ImVec2 p(a.x + inset + x * cell, a.y + inset + y * cell);
                g.dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), col);
            }
    g.dl->AddRect(ImVec2(a.x + inset, a.y + inset), ImVec2(b.x - inset, b.y - inset), col, 0.0f, 0, std::max(1.0f, cell * 0.2f));
}

void drawArrow(const GraphPaint& g, ImVec2 center, float size, bool down, ImU32 col)
{
    const ImVec2 c = g.at(center);
    const float s = size * g.scale() * 0.5f;
    if (down) g.dl->AddTriangleFilled(ImVec2(c.x - s, c.y - s * 0.55f), ImVec2(c.x + s, c.y - s * 0.55f), ImVec2(c.x, c.y + s * 0.6f), col);
    else      g.dl->AddTriangleFilled(ImVec2(c.x - s * 0.55f, c.y - s), ImVec2(c.x + s * 0.6f, c.y), ImVec2(c.x - s * 0.55f, c.y + s), col);
}

void drawThumb(const GraphPaint& g, const ImRect& r, NodeId id, NodeThumbnails& thumbs, float textSize)
{
    const ImVec2 a = g.at(r.Min), b = g.at(r.Max);
    g.dl->AddRectFilled(ImVec2(a.x - g.px(1), a.y - g.px(1)), ImVec2(b.x + g.px(1), b.y + g.px(1)), kBorder);
    g.dl->AddRectFilled(a, b, kInset);
    NodeThumbnails::View v;
    if (!thumbs.get(id, v)) return;
    if (v.empty) textCentered(g, r, textSize, kDimText, "Empty");
    else         g.dl->AddImage(v.texture, a, b, v.uv0, v.uv1);
}

} // namespace

// ---------------------------------------------------------------- text

void drawText(const GraphPaint& g, ImVec2 graphPos, float size, ImU32 color, const char* text, const char* end)
{
    const float px = size * g.scale();
    if (px < 3.5f) return;   // unreadable: leave it out
    const float q = quantizedSize(px);
    const ImVec2 p = g.at(graphPos);
    const int first = g.dl->VtxBuffer.Size;
    g.dl->AddText(ImGui::GetFont(), q, p, color, text, end);
    // The glyphs were laid out from the truncated position at the baked size: scale them
    // to the exact size around the exact position (channels share one vertex buffer).
    const float k = px / q;
    const ImVec2 o(std::trunc(p.x), std::trunc(p.y));
    for (int i = first; i < g.dl->VtxBuffer.Size; ++i) {
        ImVec2& v = g.dl->VtxBuffer[i].pos;
        v = ImVec2(p.x + (v.x - o.x) * k, p.y + (v.y - o.y) * k);
    }
}

float textWidth(float size, const char* text, const char* end)
{
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text, end).x;
}

std::string elide(const std::string& text, float size, float maxWidth)
{
    if (textWidth(size, text.c_str()) <= maxWidth) return text;
    std::string s = text;
    while (!s.empty() && textWidth(size, (s + "...").c_str()) > maxWidth) s.pop_back();
    return s + "...";
}

// ---------------------------------------------------------------- nodes

ImU32 headerColor(const Project& project, const Node& n)
{
    if (project.isMaster(n.m_meta.id)) return kHeaderOutput;
    switch (n.m_meta.type) {
    case NodeType::Raster:     return kHeaderRaster;
    case NodeType::Reference:  return kHeaderReference;
    case NodeType::Move:       return kHeaderMove;
    case NodeType::Compositor: return kHeaderCompositor;
    }
    return kHeaderRaster;
}

void paintPort(const GraphPaint& g, ImVec2 graphPos, ImU32 color, bool connected, bool hovered, bool placeholder)
{
    const ImVec2 c = g.at(graphPos);
    float r = std::max(kPortRadius * g.scale(), g.px(3.0f));
    if (hovered) r *= 1.3f;
    if (placeholder) color = (color & 0x00FFFFFFu) | (150u << IM_COL32_A_SHIFT);
    g.dl->AddCircleFilled(c, r + g.px(1.5f), kPortRing);
    g.dl->AddCircleFilled(c, r, color);
    if (!connected) g.dl->AddCircleFilled(c, r * 0.5f, kBody);   // hollow: nothing linked
}

void paintNode(const GraphPaint& g, const Project& project, const Node& n, const NodeShape& shape,
               const NodeVisual& visual, NodeThumbnails& thumbs)
{
    ImDrawList* dl = g.dl;
    const float s  = g.scale();
    const float r  = kRounding * s;
    const float bw = g.px(1.0f);
    const NodeId id = n.m_meta.id;
    const auto widgetHovered = [&](NodeControl c, uint32_t layer = 0, NodeSignal sig = NodeSignal::Main, NodeChannel ch = NodeChannel::A) {
        const auto match = [&](int i) {
            if (i < 0) return false;
            const NodeWidget& w = shape.widgets[size_t(i)];
            return w.control == c && w.layer == layer && w.signal == sig && w.channel == ch;
        };
        return match(visual.hoveredWidget) || match(visual.activeWidget);
    };

    // A value being dragged shows its previewed value (the graph only gets it on release).
    const auto scaleOf = [&](NodeSignal sig, NodeChannel ch) {
        const EditPreview& pv = project.preview();
        return pv.isScale(id, sig, ch) ? pv.scale : n.signal(sig).scale[int(ch)];
    };

    // ---- boxes ("floating" parts): shadow, border (the box grown outwards), body, header band
    const ImU32 border = kBorder;
    for (const NodeBox& box : shape.boxes) {
        const ImVec2 a = g.at(box.rect.Min), b = g.at(box.rect.Max);
        dl->AddRectFilled(ImVec2(a.x - bw, a.y), ImVec2(b.x + bw, b.y + g.px(3.0f)), kShadow, r + bw);
        dl->AddRectFilled(ImVec2(a.x - bw, a.y - bw), ImVec2(b.x + bw, b.y + bw), border, r + bw);
        dl->AddRectFilled(a, b, kBody, r);
        if (box.header > 0.0f) {
            const float hb = a.y + box.header * s;
            dl->AddRectFilled(a, ImVec2(b.x, hb), headerColor(project, n), r, ImDrawFlags_RoundCornersTop);
            dl->AddRectFilled(ImVec2(a.x, hb - bw * 0.5f), ImVec2(b.x, hb + bw * 0.5f), kSeparator);
        }
    }

    // ---- header
    const std::string title = elide(n.m_meta.label, kTitleText, kNodeWidth - 2.0f * kPad);
    textInRow(g, shape.header.Min.x + kPad, shape.header, kTitleText, kTitleColor, title.c_str());

    // ---- body: I/O labels, raster toggles, thumbnail
    if (shape.ioRow.GetHeight() > 0.0f) {
        if (shape.output()) {
            const float w = textWidth(kSmallText, "Output");
            textInRow(g, shape.ioRow.Max.x - kPad - 4.0f - w, shape.ioRow, kSmallText, kDimText, "Output");
        }
        if (shape.input(InputSlot::input()))
            textInRow(g, shape.ioRow.Min.x + kPad + 4.0f, shape.ioRow, kSmallText, kDimText, "Input");
        for (const NodeWidget& w : shape.widgets) {
            if (w.control == NodeControl::Lock)
                drawLockIcon(g, w.rect, static_cast<const RasterRootNode&>(n).locked(), widgetHovered(NodeControl::Lock));
            if (w.control == NodeControl::LockAlpha)
                drawAlphaLockIcon(g, w.rect, static_cast<const RasterRootNode&>(n).alphaLocked(), widgetHovered(NodeControl::LockAlpha));
        }
    }
    if (shape.thumb.GetWidth() > 0.0f) drawThumb(g, shape.thumb, id, thumbs, kText);

    // ---- Move offset
    if (auto* m = dynamic_cast<const MoveNode*>(&n)) {
        static const char* kAxis[2] = { "Offset X", "Offset Y" };
        const EditPreview& pv = project.preview();
        const bool previewed = pv.kind == EditPreview::Kind::MoveOffset && pv.node == id;
        const int value[2] = { previewed ? pv.x : m->offsetX(), previewed ? pv.y : m->offsetY() };
        for (int axis = 0; axis < 2; ++axis) {
            const ImRect& row = shape.offsetRow[axis];
            textInRow(g, row.Min.x + kPad, row, kText, kTextColor, kAxis[axis]);
            for (const NodeWidget& w : shape.widgets) {
                if (w.control != (axis == 0 ? NodeControl::OffsetX : NodeControl::OffsetY)) continue;
                const bool hot = widgetHovered(w.control);
                dl->AddRectFilled(g.at(w.rect.Min), g.at(w.rect.Max), hot ? kRowHovered : kInset, 3.0f * s);
                char buf[24];
                std::snprintf(buf, sizeof buf, "%d px", value[axis]);
                textCentered(g, w.rect, kText, kTextColor, buf);
            }
        }
    }

    // ---- layer stack
    if (auto* c = dynamic_cast<const CompositorNode*>(&n)) {
        for (const NodeLayerRow& row : shape.layers) {
            const ImVec2 a = g.at(row.rect.Min), b = g.at(row.rect.Max);
            if (row.placeholder) {
                dl->AddRect(a, b, IM_COL32(255, 255, 255, 40), 3.0f * s, 0, bw);
                textInRow(g, row.rect.Min.x + kPad + 2.0f, row.rect, kSmallText, kDimText, "+ New layer");
                continue;
            }
            const bool hot = widgetHovered(NodeControl::Layer, row.layer);
            dl->AddRectFilled(a, b, hot ? kRowHovered : kRow, 3.0f * s);
            if (visual.activeWidget >= 0 && shape.widgets[size_t(visual.activeWidget)].control == NodeControl::Layer &&
                shape.widgets[size_t(visual.activeWidget)].layer == row.layer)
                dl->AddRect(a, b, kAccent, 3.0f * s, 0, g.px(1.5f));
            const Node& layer = *c->layers()[row.layer];
            drawThumb(g, row.thumb, layer.m_meta.id, thumbs, kSmallText);

            // Blend mode chip: the node system composites Normal (over) only, for now.
            const ImRect chip(row.rect.Max.x - 52.0f, row.rect.GetCenter().y - 8.0f, row.rect.Max.x - 6.0f, row.rect.GetCenter().y + 8.0f);
            dl->AddRect(g.at(chip.Min), g.at(chip.Max), IM_COL32(255, 255, 255, 50), 2.0f * s, 0, bw);
            textCentered(g, chip, kSmallText, kDimText, "Normal");

            const float nameX = row.thumb.Max.x + 6.0f;
            const std::string name = elide(layer.m_meta.label, kSmallText, chip.Min.x - 4.0f - nameX);
            textInRow(g, nameX, row.rect, kSmallText, kTextColor, name.c_str());
        }
    }

    // ---- signals
    static const char* kSignalName[kSignalCount] = { "Main", "Dry", "Wet" };   // NodeSignal order
    static const char* kChannelName[kChannelCount] = { "A", "R", "G", "B" };
    for (const NodeSectionRow& sec : shape.sections) {
        const bool hot = widgetHovered(NodeControl::SectionToggle, 0, sec.signal);
        drawArrow(g, ImVec2(sec.rect.Min.x + 12.0f, sec.rect.GetCenter().y), 7.0f, sec.expanded, hot ? kTitleColor : kDimText);
        textInRow(g, sec.rect.Min.x + 21.0f, sec.rect, kText, hot ? kTitleColor : kTextColor, kSignalName[int(sec.signal)]);
        if (Node::gainByte(scaleOf(sec.signal, NodeChannel::A)) == 0) {
            const float w = textWidth(kSmallText, "off");
            textInRow(g, sec.rect.Max.x - kPad - w, sec.rect, kSmallText, kDimText, "off");
        }
    }
    for (const NodeChannelRow& row : shape.channels) {
        const float v   = scaleOf(row.signal, row.channel);
        const bool  hot = widgetHovered(NodeControl::Scale, 0, row.signal, row.channel);
        const float cy  = g.at(row.track.GetCenter()).y;
        const float x0  = g.at(row.track.Min).x, x1 = g.at(row.track.Max).x;
        const float xv  = x0 + (x1 - x0) * v;
        const float t   = std::max(g.px(2.0f), 2.5f * s);
        dl->AddRectFilled(ImVec2(x0, cy - t * 0.5f), ImVec2(x1, cy + t * 0.5f), kTrack, t * 0.5f);
        dl->AddRectFilled(ImVec2(x0, cy - t * 0.5f), ImVec2(xv, cy + t * 0.5f), kTrackFill, t * 0.5f);
        const float gw = 3.0f * s, gh = 6.5f * s;
        dl->AddRectFilled(ImVec2(xv - gw - bw, cy - gh - bw), ImVec2(xv + gw + bw, cy + gh + bw), kBorder, 2.0f * s);
        dl->AddRectFilled(ImVec2(xv - gw, cy - gh), ImVec2(xv + gw, cy + gh), hot ? kGrabActive : kGrab, 1.5f * s);

        char buf[8];
        std::snprintf(buf, sizeof buf, "%d", int(std::lround(v * 100.0f)));
        const float w = textWidth(kSmallText, buf);
        textInRow(g, row.rect.Max.x - 20.0f - w, row.rect, kSmallText, kTextColor, buf);
        textInRow(g, row.rect.Max.x - 13.0f, row.rect, kSmallText, kChannelColor[int(row.channel)], kChannelName[int(row.channel)]);
    }

}

void paintNodePorts(const GraphPaint& g, const Project& project, const NodeShape& shape, const NodeVisual& visual)
{
    for (size_t i = 0; i < shape.ports.size(); ++i) {
        const NodePort& p = shape.ports[i];
        const bool connected = p.output || project.inputAt(shape.id, p.slot) != nullptr;
        paintPort(g, p.pos, p.mask ? kMaskPort : kImagePort, connected, visual.hoveredPort == int(i), p.placeholder);
    }
}

void paintNodeOutline(const GraphPaint& g, const NodeShape& shape, const NodeVisual& visual)
{
    // Rings around the whole node (all its boxes), one shape for every state. Gaps and widths
    // are screen pixels, so the rings hug the node the same way at any zoom.
    struct Ring { ImU32 color; float width; };
    Ring rings[2];
    int count = 0;
    if (visual.active)   rings[count++] = { kActiveRaster, 2.0f };
    if (visual.selected) rings[count++] = { kSelection, 2.0f };
    if (count == 0 && visual.hovered) rings[count++] = { kBorderHover, 1.5f };

    const ImVec2 a = g.at(shape.bounds.Min), b = g.at(shape.bounds.Max);
    const float r = kRounding * g.scale();
    float gap = g.px(2.5f);   // from the node's edge to the first ring's inner edge
    for (int i = 0; i < count; ++i) {
        const float w = g.px(rings[i].width);
        const float d = gap + w * 0.5f;   // strokes are centred on their path
        g.dl->AddRect(ImVec2(a.x - d, a.y - d), ImVec2(b.x + d, b.y + d), rings[i].color, r + d, 0, w);
        gap += w + g.px(2.0f);
    }
}

// ---------------------------------------------------------------- links

LinkCurve linkCurve(const GraphPaint& g, ImVec2 from, ImVec2 to)
{
    const float d = std::clamp(std::fabs(to.x - from.x) * 0.5f, 30.0f * g.scale(), 220.0f * g.scale());
    return { from, ImVec2(from.x + d, from.y), ImVec2(to.x - d, to.y), to };
}

ImVec2 linkPoint(const LinkCurve& c, float t)
{
    const float u = 1.0f - t;
    const float w0 = u * u * u, w1 = 3.0f * u * u * t, w2 = 3.0f * u * t * t, w3 = t * t * t;
    return ImVec2(w0 * c.p0.x + w1 * c.p1.x + w2 * c.p2.x + w3 * c.p3.x,
                  w0 * c.p0.y + w1 * c.p1.y + w2 * c.p2.y + w3 * c.p3.y);
}

float linkDistance(const LinkCurve& c, ImVec2 p)
{
    constexpr int kSteps = 32;
    float best = FLT_MAX;
    ImVec2 a = c.p0;
    for (int i = 1; i <= kSteps; ++i) {
        const ImVec2 b = linkPoint(c, float(i) / kSteps);
        const ImVec2 ab(b.x - a.x, b.y - a.y), ap(p.x - a.x, p.y - a.y);
        const float len2 = ab.x * ab.x + ab.y * ab.y;
        const float t = len2 > 0.0f ? std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
        const float dx = ap.x - ab.x * t, dy = ap.y - ab.y * t;
        best = std::min(best, dx * dx + dy * dy);
        a = b;
    }
    return std::sqrt(best);
}

void paintLink(const GraphPaint& g, const LinkCurve& c, ImU32 color, bool highlight)
{
    const float w = g.px(kLinkWidth) * std::clamp(g.camera.zoom, 0.6f, 1.6f);
    if (highlight) g.dl->AddBezierCubic(c.p0, c.p1, c.p2, c.p3, IM_COL32(255, 255, 255, 70), w + g.px(4.0f));
    g.dl->AddBezierCubic(c.p0, c.p1, c.p2, c.p3, color, w);
}

void paintInsertButton(const GraphPaint& g, ImVec2 p, bool hovered)
{
    const float r = g.px(9.0f);
    g.dl->AddCircleFilled(p, r + g.px(1.0f), kPortRing);
    g.dl->AddCircleFilled(p, r, hovered ? kAccent : IM_COL32(52, 52, 52, 255));
    g.dl->AddCircle(p, r, IM_COL32(220, 220, 220, 255), 0, g.px(1.5f));
    const float a = r * 0.5f, t = g.px(1.8f);
    g.dl->AddLine(ImVec2(p.x - a, p.y), ImVec2(p.x + a, p.y), IM_COL32(240, 240, 240, 255), t);
    g.dl->AddLine(ImVec2(p.x, p.y - a), ImVec2(p.x, p.y + a), IM_COL32(240, 240, 240, 255), t);
}
