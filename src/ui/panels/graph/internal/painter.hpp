#pragma once
// Draws node items and links, transformed to screen space first so ImGui tessellates and
// antialiases at the real size: crisp at any zoom.

#include "ui/panels/graph/internal/camera.hpp"
#include "ui/panels/graph/internal/item.hpp"
#include "document/artboard.hpp"
#include "ui/render/painter.hpp"

#include <string>

class NodeThumbnails;

struct GraphPaint {
    Painter            draw;           // the graph panel's window
    const GraphCamera& camera;
    float              contentScale;   // UI pixels (line widths follow the DPI, not the zoom)
    ImFont*            font;           // labels
    ImFont*            numberFont;     // values

    float  scale() const { return camera.scale(); }
    ImVec2 at(ImVec2 g) const { return camera.toScreen(g); }
    float  px(float uiPixels) const { return uiPixels * contentScale; }
};

// How one node is being interacted with this frame.
struct NodeItemVisual {
    bool selected = false;
    bool hovered  = false;
    bool active   = false;   // the raster the canvas paints into
    int  hoveredPort   = -1;   // index into item.ports
    int  hoveredWidget = -1;   // index into item.widgets
    int  activeWidget  = -1;
};

// Text at `size` graph units, baked at quantized sizes and scaled to fit (a smooth zoom would
// bake a new size every frame). `font`: g.font when null.
void  drawText(const GraphPaint& g, ImVec2 graphPos, float size, ImU32 color, const char* text, const char* end = nullptr,
               ImFont* font = nullptr);
float textWidth(const GraphPaint& g, float size, const char* text, const char* end = nullptr, ImFont* font = nullptr);   // graph units
std::string elide(const GraphPaint& g, const std::string& text, float size, float maxWidth);

// A node paints in three layers: boxes and controls, state rings (active, selected, hovered),
// then ports, so they sit on top of every edge.
void paintNode(const GraphPaint& g, const Project& project, const Node& n, const NodeItem& item,
               const NodeItemVisual& visual, NodeThumbnails& thumbs);
void paintNodeOutline(const GraphPaint& g, const NodeItem& item, const NodeItemVisual& visual);
void paintNodePorts(const GraphPaint& g, const Project& project, const NodeItem& item, const NodeItemVisual& visual);
// How a stack's row shows: under the mouse, carried by a drag (floating over the stack), picked;
// a layer muted there; the part of it under the mouse.
struct RowLook {
    bool hot = false, lifted = false, selected = false, muted = false;
    bool eyeHot = false, foldHot = false, colorHot = false;
    bool folded = false;   // an artboard's layers folded away
};
void paintLayerRow(const GraphPaint& g, const Node& layer, const ImRect& rect, NodeThumbnails& thumbs, const RowLook& look);
// An artboard's row (the master's): its triangle, eye, colour and name. Null: gone.
void paintArtboardRow(const GraphPaint& g, const Artboard* board, const ImRect& rect, const RowLook& look);

// Links run between two screen points: out of an output (rightwards) into an input.
struct LinkCurve { ImVec2 p0, p1, p2, p3; };
LinkCurve linkCurve(const GraphPaint& g, ImVec2 from, ImVec2 to);
ImVec2    linkPoint(const LinkCurve& c, float t);
float     linkDistance(const LinkCurve& c, ImVec2 p);   // screen pixels
float     linkNearest(const LinkCurve& c, ImVec2 p);    // the t (0..1) of the curve's point nearest to p
void      paintLink(const GraphPaint& g, const LinkCurve& c, ImU32 color, bool highlight);
// A link into the pinned master: dashed, fading out on the way.
void      paintFadingLink(const GraphPaint& g, const LinkCurve& c, ImU32 color, bool highlight);
void      paintPort(const GraphPaint& g, ImVec2 graphPos, ImU32 color, bool connected, bool hovered, bool placeholder);
void      paintInsertButton(const GraphPaint& g, ImVec2 screenPos, bool hovered);

ImU32 headerColor(const Project& project, const Node& n);
