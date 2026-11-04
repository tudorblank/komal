#include "ui/panels/graph/internal/painter.hpp"
#include "ui/style/ui-style.hpp"
#include "base/math.hpp"
#include "document/project.hpp"
#include "ui/render/skins.hpp"
#include "ui/panels/graph/internal/layout.hpp"
#include "ui/style/graph-style.hpp"
#include "ui/panels/canvas/node-thumbnails.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <span>

using namespace GraphLayout;
using namespace GraphStyle;

namespace {


// Font sizes are baked in steps that grow with the size, rounded up so text is never
// rendered from a smaller bake than the screen needs.
float quantizedSize(float px)
{
    const float step = px < 12.0f ? 0.5f : px < 24.0f ? 1.0f : px < 48.0f ? 2.0f : 4.0f;
    return std::ceil(px / step - 0.01f) * step;
}

void textCentered(const GraphPaint& g, const ImRect& r, float size, ImU32 color, const char* text)
{
    const float w = textWidth(g, size, text);
    drawText(g, ImVec2(r.GetCenter().x - w * 0.5f, r.GetCenter().y - size * 0.5f), size, color, text);
}

// Text vertically centred on a row, starting at x.
void textInRow(const GraphPaint& g, float x, const ImRect& row, float size, ImU32 color, const char* text, ImFont* font = nullptr)
{
    drawText(g, ImVec2(x, row.GetCenter().y - size * 0.55f), size, color, text, nullptr, font);
}

void drawLockIcon(const GraphPaint& g, const ImRect& r, bool locked, bool hovered)
{
    const ImU32 col = locked ? colors().iconOn : hovered ? colors().text : colors().iconOff;
    const ImVec2 a = g.at(r.Min), b = g.at(r.Max);
    const float w = b.x - a.x, h = b.y - a.y;
    const float bodyTop = a.y + h * 0.45f;
    g.draw.fillRect(ImVec2(a.x + w * 0.12f, bodyTop), ImVec2(b.x - w * 0.12f, b.y), col, w * 0.12f);
    // Shackle: closed sits in the body, open is lifted and swung.
    const float rr = w * 0.26f, lift = locked ? 0.0f : h * 0.12f;
    const ImVec2 c(a.x + w * 0.5f, bodyTop - lift);
    g.draw.arc(c, rr, kPi, locked ? kTau : 1.75f * kPi, col, std::max(1.0f, w * 0.13f), 12);
    if (locked) g.draw.line(ImVec2(c.x - rr, c.y), ImVec2(c.x - rr, bodyTop), col, std::max(1.0f, w * 0.13f));
}

// Lock transparency (Photoshop's checkerboard): a 3x3 checker, five squares filled.
void drawAlphaLockIcon(const GraphPaint& g, const ImRect& r, bool locked, bool hovered)
{
    const ImU32 col = locked ? colors().iconOn : hovered ? colors().text : colors().iconOff;
    const ImVec2 a = g.at(r.Min), b = g.at(r.Max);
    const float inset = (b.x - a.x) * 0.1f;
    const float cell = (b.x - a.x - 2.0f * inset) / 3.0f;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            if ((x + y) % 2 == 0) {
                const ImVec2 p(a.x + inset + float(x) * cell, a.y + inset + float(y) * cell);
                g.draw.fillRect(p, ImVec2(p.x + cell, p.y + cell), col);
            }
    g.draw.strokeRect(ImVec2(a.x + inset, a.y + inset), ImVec2(b.x - inset, b.y - inset), col, 0.0f, std::max(1.0f, cell * 0.2f));
}

// Shown: an open eye. Muted: a small dark square in its place.
void drawEye(const GraphPaint& g, const ImRect& r, bool shown, bool hovered)
{
    const ImVec2 a = g.at(r.Min), b = g.at(r.Max);
    const float w = b.x - a.x, h = b.y - a.y, s = g.scale();
    if (!shown) {
        const float in = w * 0.2f;
        const ImVec2 p0(a.x + in, a.y + in), p1(b.x - in, b.y - in);
        g.draw.fillRect(p0, p1, colors().inset, 2.0f * s);
        g.draw.strokeRect(p0, p1, hovered ? colors().borderHover : colors().border, 2.0f * s, g.px(1.0f));
        return;
    }
    const ImU32 col = hovered ? colors().title : colors().text;
    const float cx = a.x + w * 0.5f, cy = a.y + h * 0.5f, hw = w * 0.47f, lid = h * 0.42f;
    const float t = std::max(1.0f, w * 0.09f);
    const ImVec2 l(cx - hw, cy), rt(cx + hw, cy);
    g.draw.bezier(l, ImVec2(cx - hw * 0.45f, cy - lid), ImVec2(cx + hw * 0.45f, cy - lid), rt, col, t);
    g.draw.bezier(l, ImVec2(cx - hw * 0.45f, cy + lid), ImVec2(cx + hw * 0.45f, cy + lid), rt, col, t);
    g.draw.fillCircle(ImVec2(cx, cy), h * 0.19f, col);
}

// A rectangle's outline in dashes, screen space.
void dashedRect(const GraphPaint& g, ImVec2 a, ImVec2 b, ImU32 col, float width)
{
    const float dash = g.px(5.0f), gap = g.px(3.0f);
    const auto edge = [&](ImVec2 p, ImVec2 q) {
        const float len = std::hypot(q.x - p.x, q.y - p.y);
        if (len <= 0.0f) return;
        const ImVec2 d((q.x - p.x) / len, (q.y - p.y) / len);
        for (float t = 0.0f; t < len; t += dash + gap) {
            const float e = std::min(t + dash, len);
            g.draw.line(ImVec2(p.x + d.x * t, p.y + d.y * t), ImVec2(p.x + d.x * e, p.y + d.y * e), col, width);
        }
    };
    edge(a, ImVec2(b.x, a.y));
    edge(ImVec2(b.x, a.y), b);
    edge(b, ImVec2(a.x, b.y));
    edge(ImVec2(a.x, b.y), a);
}

void drawArrow(const GraphPaint& g, ImVec2 center, float size, bool down, ImU32 col)
{
    const ImVec2 c = g.at(center);
    const float s = size * g.scale() * 0.5f;
    if (down) g.draw.fillTriangle(ImVec2(c.x - s, c.y - s * 0.55f), ImVec2(c.x + s, c.y - s * 0.55f), ImVec2(c.x, c.y + s * 0.6f), col);
    else      g.draw.fillTriangle(ImVec2(c.x - s * 0.55f, c.y - s), ImVec2(c.x + s * 0.6f, c.y), ImVec2(c.x - s * 0.55f, c.y + s), col);
}

void drawThumb(const GraphPaint& g, const ImRect& r, NodeId id, NodeThumbnails& thumbs, float textSize)
{
    const ImVec2 a = g.at(r.Min), b = g.at(r.Max);
    g.draw.fillRect(ImVec2(a.x - g.px(1), a.y - g.px(1)), ImVec2(b.x + g.px(1), b.y + g.px(1)), colors().border);
    g.draw.fillRect(a, b, colors().inset);
    NodeThumbnails::View v;
    if (!thumbs.get(id, v)) return;
    if (v.empty) {
        textCentered(g, r, textSize, colors().dimText, "Empty");
        return;
    }
    const auto at = [&](ImVec2 f) { return ImVec2(a.x + (b.x - a.x) * f.x, a.y + (b.y - a.y) * f.y); };   // the slot's proportions are the rect's
    g.draw.image(v.texture, at(v.fit0), at(v.fit1), v.uv0, v.uv1);
}

}

// ---------------------------------------------------------------- text

void drawText(const GraphPaint& g, ImVec2 graphPos, float size, ImU32 color, const char* text, const char* end, ImFont* font)
{
    const float px = size * g.scale();
    if (px < 3.5f) return;   // unreadable: leave it out
    const float q = quantizedSize(px);
    g.draw.scaledText(font ? font : g.font, q, px, g.at(graphPos), color, text, end);
}

float textWidth(const GraphPaint& g, float size, const char* text, const char* end, ImFont* font)
{
    return (font ? font : g.font)->CalcTextSizeA(size, FLT_MAX, 0.0f, text, end).x;
}

std::string elide(const GraphPaint& g, const std::string& text, float size, float maxWidth)
{
    if (textWidth(g, size, text.c_str()) <= maxWidth) return text;
    std::string s = text;
    while (!s.empty() && textWidth(g, size, (s + "...").c_str()) > maxWidth) s.pop_back();
    return s + "...";
}

// ---------------------------------------------------------------- nodes

ImU32 headerColor(const Project& project, const Node& n)
{
    return project.isMaster(n.id()) ? colors().headerOutput : kindHeader(n.type());
}

void paintPort(const GraphPaint& g, ImVec2 graphPos, ImU32 color, bool connected, bool hovered, bool placeholder)
{
    const ImVec2 c = g.at(graphPos);
    float r = std::max(kPortRadius * g.scale(), g.px(3.0f));
    if (hovered) r *= 1.3f;
    if (placeholder) color = withAlpha(color, kPlaceholderAlpha);
    g.draw.fillCircle(c, r + g.px(1.5f), colors().portRing);
    g.draw.fillCircle(c, r, color);
    if (!connected) g.draw.fillCircle(c, r * 0.5f, colors().body);   // hollow: nothing linked
}

void paintNode(const GraphPaint& g, const Project& project, const Node& n, const NodeItem& item,
               const NodeItemVisual& visual, NodeThumbnails& thumbs)
{
    const Painter& draw = g.draw;
    const float s  = g.scale();
    const float r  = kRounding * s;
    const float bw = g.px(1.0f);
    const NodeId id = n.id();
    const auto widgetHovered = [&](const NodeItemControl& c) {
        const auto match = [&](int i) { return i >= 0 && item.widgets[size_t(i)].control == c; };
        return match(visual.hoveredWidget) || match(visual.activeWidget);
    };
    const NodeSettings settings = n.settings();

    // A value being dragged shows its previewed value (the graph only gets it on release).
    const auto scaleOf = [&](NodeSignal sig, NodeChannel ch) {
        const EditPreview* pv = project.preview();
        if (const std::optional<float> v = pv ? pv->scaleOf(id, sig, ch) : std::nullopt) return *v;
        return n.signal(sig).scale[int(ch)];
    };

    // ---- boxes ("floating" parts): shadow, border (the box grown outwards), body, header band
    const ImU32 border = colors().border;
    for (const NodeItemBox& box : item.boxes) {
        const ImVec2 a = g.at(box.rect.Min), b = g.at(box.rect.Max);
        draw.fillRect(ImVec2(a.x - bw, a.y), ImVec2(b.x + bw, b.y + g.px(3.0f)), colors().shadow, r + bw);
        draw.fillRect(ImVec2(a.x - bw, a.y - bw), ImVec2(b.x + bw, b.y + bw), border, r + bw);
        draw.fillRect(a, b, colors().body, r);
        if (box.header > 0.0f) {
            const float hb = a.y + box.header * s;
            draw.fillRect(a, ImVec2(b.x, hb), headerColor(project, n), r, ImDrawFlags_RoundCornersTop);
            draw.fillRect(ImVec2(a.x, hb - bw * 0.5f), ImVec2(b.x, hb + bw * 0.5f), colors().separator);
        }
    }

    // ---- header
    const std::string title = elide(g, n.label(), kTitleText, kNodeWidth - 2.0f * kPad);
    textInRow(g, item.header.Min.x + kPad, item.header, kTitleText, colors().title, title.c_str());

    // ---- body: I/O labels, raster toggles, thumbnail
    if (item.ioRow) {
        const ImRect& io = *item.ioRow;
        if (item.output()) {
            const float w = textWidth(g, kSmallText, "Output");
            textInRow(g, io.Max.x - kPad - 4.0f - w, io, kSmallText, colors().dimText, "Output");
        }
        if (item.input(InputSlot::input()))
            textInRow(g, io.Min.x + kPad + 4.0f, io, kSmallText, colors().dimText, "Input");
    }
    if (item.thumb) drawThumb(g, *item.thumb, id, thumbs, kText);

    // ---- its kind's settings: the controls on its rows
    for (const NodeItemPropertyRow& row : item.properties) textInRow(g, row.rect.Min.x + kPad, row.rect, kText, colors().text, row.label);
    const OffsetEdit* dragged = project.preview() ? project.preview()->offsetOf(id) : nullptr;   // shown as dragged
    for (const NodeItemWidget& w : item.widgets) {
        if (const auto* raster = std::get_if<RasterSettings>(&settings)) {
            if (w.as<LockControl>())      drawLockIcon(g, w.rect, raster->locked, widgetHovered(w.control));
            if (w.as<LockAlphaControl>()) drawAlphaLockIcon(g, w.rect, raster->alphaLocked, widgetHovered(w.control));
        }
        const auto* offset = w.as<OffsetControl>();
        const auto* move   = std::get_if<MoveSettings>(&settings);
        if (offset && move) {
            const int value = offset->axis == 0 ? (dragged ? dragged->x : move->x) : (dragged ? dragged->y : move->y);
            draw.fillRect(g.at(w.rect.Min), g.at(w.rect.Max), widgetHovered(w.control) ? colors().rowHovered : colors().inset, 3.0f * s);
            char buf[24];
            std::snprintf(buf, sizeof buf, "%d px", value);
            textCentered(g, w.rect, kText, colors().text, buf);
        }
    }

    // ---- layer stack
    const std::vector<NodePtr>& layers = n.links().layers;
    for (const NodeItemLayerRow& row : item.layers) {
        const ImVec2 a = g.at(row.rect.Min), b = g.at(row.rect.Max);
        if (const auto* gap = row.as<NodeItemLayerRow::Gap>()) {   // room where the dragged link or rows land, or the line
            if (gap->line) {
                draw.fillRect(a, b, colors().accent, b.y - a.y);
            } else {
                draw.fillRect(a, b, colors().gapFill, 3.0f * s);
                draw.strokeRect(a, b, colors().accent, 3.0f * s, g.px(1.5f));
            }
        } else if (row.as<NodeItemLayerRow::Origin>()) {   // a row a drag carries: its spot, kept ("moving away from here")
            dashedRect(g, a, b, colors().originEdge, g.px(1.5f));
        } else if (const auto* empty = row.as<NodeItemLayerRow::Empty>()) {  // a stack with nothing in it yet
            draw.fillRect(a, b, empty->target ? colors().gapFill : colors().inset, 3.0f * s);
            if (empty->target) draw.strokeRect(a, b, colors().accent, 3.0f * s, g.px(1.5f));   // a drop lands here
            else               draw.strokeRect(a, b, colors().emptyStack, 3.0f * s, bw);
            textCentered(g, row.rect, kSmallText, colors().dimText, "Drop nodes here");
        } else if (const auto* layer = row.as<NodeItemLayerRow::Layer>(); layer && layer->index < layers.size()) {
            const Node& l = *layers[layer->index];
            const auto* stack = dynamic_cast<const CompositorNode*>(&n);
            const bool eyeHot = widgetHovered(LayerMuteControl{ layer->index });
            paintLayerRow(g, l, row.rect, thumbs, { .hot = eyeHot || widgetHovered(LayerControl{ layer->index }), .selected = row.selected,
                                                    .muted = stack && stack->muted(l), .eyeHot = eyeHot });
        } else if (const auto* board = row.as<NodeItemLayerRow::Artboard>()) {
            const ArtboardId k = board->key;
            const bool parts[3] = { widgetHovered(ArtboardFoldControl{ k }), widgetHovered(ArtboardMuteControl{ k }), widgetHovered(ArtboardColorControl{ k }) };
            paintArtboardRow(g, project.artboard(k), row.rect,
                             { .hot = parts[0] || parts[1] || parts[2] || widgetHovered(ArtboardControl{ k }), .selected = row.selected,
                               .eyeHot = parts[1], .foldHot = parts[0], .colorHot = parts[2], .folded = board->folded });
        }
    }

    // ---- the artboard a drag would land in: outlined
    if (item.dropInto) {
        const float d = g.px(2.0f);
        const ImVec2 a = g.at(item.dropInto->Min), b = g.at(item.dropInto->Max);
        draw.strokeRect(ImVec2(a.x - d, a.y - d), ImVec2(b.x + d, b.y + d), colors().accent, 4.0f * s, g.px(2.0f));
    }

    // ---- signals
    for (const NodeItemSectionRow& sec : item.sections) {
        const bool hot = widgetHovered(SectionControl{ sec.signal });
        drawArrow(g, ImVec2(sec.rect.Min.x + 12.0f, sec.rect.GetCenter().y), 7.0f, sec.expanded, hot ? colors().title : colors().dimText);
        textInRow(g, sec.rect.Min.x + 21.0f, sec.rect, kText, hot ? colors().title : colors().text, signalLabel(sec.signal));
        if (Node::gainByte(scaleOf(sec.signal, NodeChannel::A)) == 0) {
            const float w = textWidth(g, kSmallText, "off");
            textInRow(g, sec.rect.Max.x - kPad - w, sec.rect, kSmallText, colors().dimText, "off");
        }
    }
    for (const NodeItemChannelRow& row : item.channels) {
        const float v   = scaleOf(row.signal, row.channel);
        const bool  hot = widgetHovered(ScaleControl{ row.signal, row.channel });
        const float cy  = g.at(row.track.GetCenter()).y;
        const float x0  = g.at(row.track.Min).x, x1 = g.at(row.track.Max).x;
        const float xv  = x0 + (x1 - x0) * v;
        Skin::slider(draw, Style::slider(s, Style::Tone::Node), x0, x1, cy, xv, hot);   // the app's slider, at the graph's zoom

        char buf[8];
        std::snprintf(buf, sizeof buf, "%d", int(std::lround(v * 100.0f)));
        const float w = textWidth(g, kSmallText, buf, nullptr, g.numberFont);
        textInRow(g, row.rect.Max.x - 20.0f - w, row.rect, kSmallText, colors().text, buf, g.numberFont);
        textInRow(g, row.rect.Max.x - 13.0f, row.rect, kSmallText, colors().channel[int(row.channel)], channelLabel(row.channel));
    }

}

namespace {

// A stack row's band: its shadow while carried, its fill (picked: tinted, and kept so), its edge.
void paintRowBand(const GraphPaint& g, const ImRect& rect, const RowLook& look)
{
    const Painter& draw = g.draw;
    const float s = g.scale();
    const ImVec2 a = g.at(rect.Min), b = g.at(rect.Max);
    if (look.lifted) draw.fillRect(ImVec2(a.x, a.y + g.px(2.0f)), ImVec2(b.x, b.y + g.px(5.0f)), colors().liftShadow, 3.0f * s);
    draw.fillRect(a, b, look.hot || look.lifted ? colors().rowHovered : colors().row, 3.0f * s);
    if (look.selected) draw.fillRect(a, b, colors().rowSelected, 3.0f * s);
    if (look.lifted) draw.strokeRect(a, b, colors().accent, 3.0f * s, g.px(1.5f));
}

}

void paintLayerRow(const GraphPaint& g, const Node& layer, const ImRect& rect, NodeThumbnails& thumbs, const RowLook& look)
{
    const Painter& draw = g.draw;
    const float s = g.scale(), bw = g.px(1.0f);
    paintRowBand(g, rect, look);
    drawEye(g, layerEyeRect(rect), !look.muted, look.eyeHot);
    const ImRect thumb = layerThumbRect(rect);
    drawThumb(g, thumb, layer.id(), thumbs, kSmallText);

    // Blend mode chip: the node system composites Normal (over) only, for now.
    const ImRect chip(rect.Max.x - 52.0f, rect.GetCenter().y - 8.0f, rect.Max.x - 6.0f, rect.GetCenter().y + 8.0f);
    draw.strokeRect(g.at(chip.Min), g.at(chip.Max), look.selected ? colors().selectedEdge : colors().chipEdge, 2.0f * s, bw);
    textCentered(g, chip, kSmallText, look.selected ? colors().selectedText : colors().dimText, "Normal");

    const float nameX = thumb.Max.x + 6.0f;
    const std::string name = elide(g, layer.label(), kSmallText, chip.Min.x - 4.0f - nameX);
    textInRow(g, nameX, rect, kSmallText, look.selected ? colors().selectedText : look.muted ? colors().dimText : colors().text, name.c_str());
}

void paintArtboardRow(const GraphPaint& g, const Artboard* board, const ImRect& rect, const RowLook& look)
{
    paintRowBand(g, rect, look);
    if (!board) return;
    const float s = g.scale();
    const ImRect fold = artboardFoldRect(rect);
    drawArrow(g, fold.GetCenter(), 7.0f, !look.folded, look.foldHot ? colors().title : colors().dimText);
    drawEye(g, artboardEyeRect(rect), board->visible, look.eyeHot);

    // Its colour: over a 2x2 checkerboard where it's see-through.
    const ImRect sw = artboardColorRect(rect);
    const ImVec2 a = g.at(sw.Min), b = g.at(sw.Max), m((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float r = 2.0f * s;
    if (board->color.a < 255) {
        g.draw.fillRect(a, b, colors().swatchCheckerB, r);
        g.draw.fillRect(a, m, colors().swatchCheckerA, r, ImDrawFlags_RoundCornersTopLeft);
        g.draw.fillRect(m, b, colors().swatchCheckerA, r, ImDrawFlags_RoundCornersBottomRight);
    }
    g.draw.fillRect(a, b, board->color.packed(), r);
    g.draw.strokeRect(a, b, look.colorHot ? colors().borderHover : colors().border, r, g.px(1.0f));

    const float nameX = sw.Max.x + 7.0f;
    const std::string name = elide(g, board->name, kText, rect.Max.x - 6.0f - nameX);
    const ImU32 ink = look.selected ? colors().selectedText : board->visible ? colors().title : colors().dimText;
    textInRow(g, nameX, rect, kText, ink, name.c_str());
}

void paintNodePorts(const GraphPaint& g, const Project& project, const NodeItem& item, const NodeItemVisual& visual)
{
    for (size_t i = 0; i < item.ports.size(); ++i) {
        const NodeItemPort& p = item.ports[i];
        const bool connected = p.output || project.inputAt(item.id, p.slot) != nullptr;
        paintPort(g, p.pos, p.mask ? colors().maskPort : colors().imagePort, connected, visual.hoveredPort == int(i), p.placeholder);
    }
}

void paintNodeOutline(const GraphPaint& g, const NodeItem& item, const NodeItemVisual& visual)
{
    // One ring per state around all the node's boxes; gaps and widths in screen pixels.
    struct Ring { ImU32 color; float width; };
    Ring rings[2];
    int count = 0;
    if (visual.active)   rings[count++] = { colors().activeRaster, 2.0f };
    if (visual.selected) rings[count++] = { colors().selection, 2.0f };
    if (count == 0 && visual.hovered) rings[count++] = { colors().borderHover, 1.5f };

    const ImVec2 a = g.at(item.bounds.Min), b = g.at(item.bounds.Max);
    const float r = kRounding * g.scale();
    float gap = g.px(2.5f);   // from the node's edge to the first ring's inner edge
    for (int i = 0; i < count; ++i) {
        const float w = g.px(rings[i].width);
        const float d = gap + w * 0.5f;   // strokes are centred on their path
        g.draw.strokeRect(ImVec2(a.x - d, a.y - d), ImVec2(b.x + d, b.y + d), rings[i].color, r + d, w);
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

namespace {

// The curve as 32 straight segments: the point nearest to p, its distance and its t.
struct Nearest { float distance2, t; };
Nearest nearestOnLink(const LinkCurve& c, ImVec2 p)
{
    constexpr int kSteps = 32;
    Nearest best{ FLT_MAX, 0.0f };
    ImVec2 a = c.p0;
    for (int i = 1; i <= kSteps; ++i) {
        const ImVec2 b = linkPoint(c, float(i) / kSteps);
        const ImVec2 ab(b.x - a.x, b.y - a.y), ap(p.x - a.x, p.y - a.y);
        const float len2 = ab.x * ab.x + ab.y * ab.y;
        const float t = len2 > 0.0f ? std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
        const float dx = ap.x - ab.x * t, dy = ap.y - ab.y * t;
        if (dx * dx + dy * dy < best.distance2) best = { dx * dx + dy * dy, (float(i - 1) + t) / kSteps };
        a = b;
    }
    return best;
}

}

float linkDistance(const LinkCurve& c, ImVec2 p) { return std::sqrt(nearestOnLink(c, p).distance2); }
float linkNearest(const LinkCurve& c, ImVec2 p)  { return nearestOnLink(c, p).t; }

void paintLink(const GraphPaint& g, const LinkCurve& c, ImU32 color, bool highlight)
{
    const float w = g.px(kLinkWidth) * std::clamp(g.camera.zoom, 0.6f, 1.6f);
    if (highlight) g.draw.bezier(c.p0, c.p1, c.p2, c.p3, colors().linkGlow, w + g.px(4.0f));
    g.draw.bezier(c.p0, c.p1, c.p2, c.p3, color, w);
}

void paintFadingLink(const GraphPaint& g, const LinkCurve& c, ImU32 color, bool highlight)
{
    const float w = g.px(kLinkWidth) * std::clamp(g.camera.zoom, 0.6f, 1.6f);
    if (highlight) g.draw.bezier(c.p0, c.p1, c.p2, c.p3, colors().fadingGlow, w + g.px(4.0f));

    // The curve as a polyline, with the distance along it at each point.
    constexpr int kSteps = 64;
    ImVec2 pts[kSteps + 1];
    float  along[kSteps + 1];
    pts[0] = c.p0;
    along[0] = 0.0f;
    for (int i = 1; i <= kSteps; ++i) {
        pts[i] = linkPoint(c, float(i) / kSteps);
        const float dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y;
        along[i] = along[i - 1] + std::sqrt(dx * dx + dy * dy);
    }
    const float total = along[kSteps];
    if (total <= 0.0f) return;
    const auto at = [&](float d) {
        const int i = std::clamp(int(std::upper_bound(along, along + kSteps + 1, d) - along) - 1, 0, kSteps - 1);
        const float t = along[i + 1] > along[i] ? (d - along[i]) / (along[i + 1] - along[i]) : 0.0f;
        return ImVec2(pts[i].x + (pts[i + 1].x - pts[i].x) * t, pts[i].y + (pts[i + 1].y - pts[i].y) * t);
    };

    const float dash = g.px(8.0f), gap = g.px(5.0f);
    const int alpha = int((color >> IM_COL32_A_SHIFT) & 0xFF);
    ImVec2 poly[kSteps + 2];
    for (float d0 = 0.0f; d0 < total; d0 += dash + gap) {
        const float d1 = std::min(d0 + dash, total);
        int n = 0;
        poly[n++] = at(d0);
        for (int i = 1; i < kSteps && n < kSteps + 1; ++i)
            if (along[i] > d0 && along[i] < d1) poly[n++] = pts[i];
        poly[n++] = at(d1);
        const float t = (d0 + d1) * 0.5f / total;   // 0 at the source, 1 at the master
        const int a = int(float(alpha) * (1.0f - 0.7f * t));
        g.draw.polyline(std::span<const ImVec2>(poly, size_t(n)), withAlpha(color, uint8_t(a)), w);
    }
}

void paintInsertButton(const GraphPaint& g, ImVec2 p, bool hovered)
{
    const float r = g.px(9.0f);
    g.draw.fillCircle(p, r + g.px(1.0f), colors().portRing);
    g.draw.fillCircle(p, r, hovered ? colors().accent : colors().insertFill);
    g.draw.strokeCircle(p, r, colors().insertRim, g.px(1.5f));
    const float a = r * 0.5f, t = g.px(1.8f);
    g.draw.line(ImVec2(p.x - a, p.y), ImVec2(p.x + a, p.y), colors().insertPlus, t);
    g.draw.line(ImVec2(p.x, p.y - a), ImVec2(p.x, p.y + a), colors().insertPlus, t);
}
