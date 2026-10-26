#pragma once
// Draws node items and links. Everything is transformed to screen space before it reaches
// the draw list, so ImGui tessellates curves and antialiases edges for the size they really
// have on screen: crisp at any zoom, with no vertex rescaling afterwards.

#include "graph/camera.hpp"
#include "graph/node-item/item.hpp"

#include <string>

class NodeItemThumbnails;

struct GraphPaint {
    ImDrawList*        dl;
    const GraphCamera& camera;
    float              contentScale;   // UI pixels (line widths follow the DPI, not the zoom)

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

// Text at `size` graph units. Glyphs are baked at a few quantized sizes (a smooth zoom would
// otherwise bake a new font size every frame) and scaled to the exact size. `font`:
// graphFont() when null (graphNumberFont() for values).
void  drawText(const GraphPaint& g, ImVec2 graphPos, float size, ImU32 color, const char* text, const char* end = nullptr,
               ImFont* font = nullptr);
float textWidth(float size, const char* text, const char* end = nullptr, ImFont* font = nullptr);   // graph units
std::string elide(const std::string& text, float size, float maxWidth);

// A node is painted in three layers, in this order: its boxes and controls, the outline of
// its state (active raster, selected, hovered: one ring each around the whole node), then
// its ports, so they always sit on top of every edge.
void paintNode(const GraphPaint& g, const Project& project, const Node& n, const NodeItem& item,
               const NodeItemVisual& visual, NodeItemThumbnails& thumbs);
void paintNodeOutline(const GraphPaint& g, const NodeItem& item, const NodeItemVisual& visual);
void paintNodePorts(const GraphPaint& g, const Project& project, const NodeItem& item, const NodeItemVisual& visual);
// One row of a layer stack; lifted: the row being dragged, floating over the stack.
void paintLayerRow(const GraphPaint& g, const Node& layer, const ImRect& rect, NodeItemThumbnails& thumbs, bool hot, bool lifted);

// Links run between two screen points: out of an output (rightwards) into an input.
struct LinkCurve { ImVec2 p0, p1, p2, p3; };
LinkCurve linkCurve(const GraphPaint& g, ImVec2 from, ImVec2 to);
ImVec2    linkPoint(const LinkCurve& c, float t);
float     linkDistance(const LinkCurve& c, ImVec2 p);   // screen pixels
float     linkNearest(const LinkCurve& c, ImVec2 p);    // the t (0..1) of the curve's point nearest to p
void      paintLink(const GraphPaint& g, const LinkCurve& c, ImU32 color, bool highlight);
// A link into the pinned master: dashed, fading out on its way there (it may cross the panel).
void      paintFadingLink(const GraphPaint& g, const LinkCurve& c, ImU32 color, bool highlight);
void      paintPort(const GraphPaint& g, ImVec2 graphPos, ImU32 color, bool connected, bool hovered, bool placeholder);
void      paintInsertButton(const GraphPaint& g, ImVec2 screenPos, bool hovered);

ImU32 headerColor(const Project& project, const Node& n);
