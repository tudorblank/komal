#pragma once
// What's under a point of the graph view: one type per kind of thing, each holding only what it
// means (a port's index is a port's, a link's is a link's). The graph view asks hitTest() once a
// frame, and everything it does with the pointer reads that one answer: pressing, hovering,
// dropping a link, painting the hover.
//
//   if (const PortHit* p = hit.as<PortHit>()) ... item->ports[p->port]
//   hit.node()   the node it's on, whatever part: kNoNodeId if none
//   hit.link()   the link it's on, or its insert button: -1 if none

#include "graph/camera.hpp"
#include "graph/node-item/item.hpp"
#include "graph/painter.hpp"

#include <span>
#include <type_traits>
#include <variant>

class GraphMinimap;
class GraphScene;

struct PortHit    { NodeId node; int port; };     // NodeItem::ports[port]
struct WidgetHit  { NodeId node; int widget; };   // NodeItem::widgets[widget]
struct NodeHit    { NodeId node; };               // the rest of a node: its header, its body
struct InsertHit  { int link; };                  // the insert button on a hovered link (GraphScene::links()[link])
struct LinkHit    { int link; };
struct MinimapHit {};

class GraphHit {
public:
    using Target = std::variant<std::monostate, PortHit, WidgetHit, NodeHit, InsertHit, LinkHit, MinimapHit>;

    GraphHit() = default;
    template <class T> requires std::is_constructible_v<Target, T>
    GraphHit(T target) : m_target(target) {}   // from any one of them

    template <class T> const T* as() const { return std::get_if<T>(&m_target); }
    template <class T> bool is() const { return std::holds_alternative<T>(m_target); }
    bool   empty() const { return is<std::monostate>(); }
    NodeId node() const;
    int    link() const;
    int    port(NodeId id) const;     // id's port it's on: -1 if not one
    int    widget(NodeId id) const;   // id's widget it's on: -1 if not one

private:
    Target m_target;
};

// How far from a thing the pointer still hits it.
namespace HitReach {
constexpr float kPort     = 9.0f;    // graph units around a port's centre ...
constexpr float kPortPx   = 7.0f;    // ... and never less than these UI pixels (zoomed out)
constexpr float kLinkPx   = 6.0f;    // UI pixels from a link's curve
constexpr float kInsertPx = 10.0f;   // UI pixels from the insert button's centre
}

// The graph view as hit testing sees it, front to back: the pinned master, the minimap, the
// other nodes (the scene's z order), the insert button of the link hovered, the links.
struct HitView {
    const GraphScene&          scene;
    NodeId                     pinned;          // drawn over everything, in its own space
    const GraphCamera&         camera;          // everything else's space
    const GraphCamera&         pinnedCamera;
    const GraphMinimap&        minimap;
    std::span<const LinkCurve> curves;          // screen, one per scene link
    float                      contentScale;    // UI pixels to screen pixels
    int                        hoveredLink;     // last frame's (-1: none) ...
    ImVec2                     insertAt;        // ... and its insert button, screen
};

GraphHit hitTest(const HitView& view, ImVec2 mouse);
// Where on a link its insert button is: the point nearest to the mouse, kept off the ports at
// its ends. Screen.
ImVec2   insertPoint(const LinkCurve& curve, ImVec2 mouse);
GraphHit hitNodeItem(NodeId id, const NodeItem& item, ImVec2 graphPoint, float portReach);   // ports, widgets, boxes

// What a hit is on, in the scene it was tested against (null: not there any more).
const NodeItemPort*   portOf(const GraphScene& scene, const PortHit& hit);
const NodeItemWidget* widgetOf(const GraphScene& scene, const WidgetHit& hit);
