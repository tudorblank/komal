#pragma once

#include "base/deadline.hpp"
#include "command/actions.hpp"
#include "document/node-selection.hpp"
#include "graph/camera.hpp"
#include "graph/hit.hpp"
#include "graph/minimap.hpp"
#include "graph/painter.hpp"
#include "graph/scene.hpp"
#include "ui/graph-panel/graph-menus.hpp"
#include "ui/panel.hpp"
#include "ui/panel-surface.hpp"

#include <array>
#include <functional>
#include <variant>
#include <vector>

struct InputFrame;
class Project;

// The node graph editor: a view of a GraphScene (the items and links, kept in step with the
// project) through a camera, with the master pinned top-right and a minimap.
//
// Edits go one way: input runs actions, which change the project, whose events update the scene.
// The selection is the project's too (the canvas outlines it): read there, changed by selection.set.
// The view keeps what's screen space (link curves, placed again when a link or a camera moved;
// the minimap's layout), and each frame paints and hit-tests the items as they are.
class GraphPanel : public Panel {
public:
    PanelInfo info() const override;
    bool init() override;   // node thumbnails: the canvas engine's
    void shutdown() override;
    void draw() override;
    double nextFrameIn() const override;
    // A file dropped at `screen` (`window` topmost): true if it lands on the graph, with the
    // position for a new node centred under it.
    bool dropTarget(const ImGuiWindow* window, ImVec2 screen, ImVec2& nodePos) const;

private:
    static constexpr float       kPinMargin   = 12.0f;   // points between the pinned master and the panel's edges

    // A left press on the graph: where it started, and what it became. Each kind of drag holds
    // only what it needs.
    struct Press {
        GraphHit hit;
        ImVec2 mouse, graph;          // screen; graph (the pressed node's space)
        bool   ctrl = false, shift = false;
    };
    struct PressNode  { std::vector<NodeId> nodes; bool wasSelected; };   // a click, or MoveNodes once it moves
    struct MoveNodes  { ImVec2 delta; };                                   // node.drag, graph units
    struct PressEmpty { NodeSelection::Set base; };                        // a click on nothing, or RubberBand once it moves
    struct RubberBand { NodeSelection::Set base; };                        // what Ctrl / Shift keep selected
    struct LinkDrag {
        NodeId    node;                    // the end that stays put
        bool      fromOutput = true;       // that end is an output: looking for an input
        InputSlot slot{};                  // ... or that input
        bool      pickedUp = false;        // lifted off an input (link.move)
        NodeId    gapStack = kNoNodeId;    // the stack it would land in: a gap opens there next frame
        int       gapAt = -1;
    };
    struct ScaleDrag  { ScaleControl control; ActionArgs args; };               // signal.scrub, as last dragged
    struct OffsetDrag { OffsetControl control; int start; ActionArgs args; };   // move.scrub
    struct LayerDrag {
        NodeId   node;                     // the compositor
        uint32_t layer;                    // stack index of the row being dragged
        float    grab;                     // mouse below the row's top, graph units
        bool     lifted = false;           // past the drag threshold: floats, with a gap where it lands
        int      at = -1;                  // that gap: a position among the other layers
    };
    struct MinimapDrag {};
    using Drag = std::variant<std::monostate, PressNode, MoveNodes, PressEmpty, RubberBand, LinkDrag, ScaleDrag, OffsetDrag,
                              LayerDrag, MinimapDrag>;
    struct LinkTarget {
        bool      valid = false;
        NodeId    node = kNoNodeId;
        InputSlot slot;
        ImVec2    pos;                // screen
    };

    GraphPaint paintContext(ImDrawList* dl, const GraphCamera& camera) const;   // the graph's fonts, at the UI scale
    const GraphCamera& cameraFor(NodeId id) const { return m_scene.isMaster(id) ? m_pinned : m_camera; }   // whose space its node item is in
    void updateStackGap();            // the scene's: where a drag would drop into a stack
    void layoutOverlays(ImVec2 canvasMin, ImVec2 canvasMax);   // the pinned master and the navigator
    void placeLinkCurves(const GraphPaint& g);   // when a link or a camera moved
    GraphHit hitAt(ImVec2 mouse) const;   // graph/hit.hpp, over this frame's layout

    void handleMouse(const GraphHit& hit, bool hovered);
    void registerActions(ActionRegistry& actions);
    void pressNode(NodeId id);
    void pressWidget(const WidgetHit& hit, bool doubleClick);
    void startLink(const PortHit& hit);
    void updateDrag();
    void endDrag(const GraphHit& hit);   // the button went up
    bool dragging() const { return !std::holds_alternative<std::monostate>(m_drag); }
    LinkTarget linkTarget(const LinkDrag& link, const GraphHit& hit) const;
    uint32_t   stackPosition(NodeId id, float y) const;   // where in its stack something dropped at graph y goes
    void finishLink(const LinkDrag& link, const GraphHit& hit);
    const NodeSelection& selection() const;
    void select(const NodeSelection& next);   // selection.set, if it changes anything
    void deleteSelection();
    void frameNodes(bool selectionOnly, ImVec2 canvasSize);
    void showInCanvas(const std::vector<NodeId>& ids);   // the canvas frames their output's world bounds
    int  activeWidget(NodeId id) const;   // index of the widget being dragged, or -1

    void paint(const GraphPaint& g, const GraphHit& hit, ImVec2 canvasMin, ImVec2 canvasMax);
    void paintGrid(const GraphPaint& g, ImVec2 canvasMin, ImVec2 canvasMax) const;

    float uiScale() const { return ui().host.uiScale; }

    PanelSurface m_surface;            // the graph, this frame
    GraphCamera m_camera;
    GraphCamera m_pinned;              // the master's space: 100%, its item in the top-right corner
    float       m_pinnedScroll = 0.0f; // screen pixels, when the master is taller than the panel
    ImRect      m_pinnedRect;          // the master on screen
    GraphMinimap m_minimap;
    std::vector<GraphMinimap::Edge> m_minimapEdges;   // built with the links
    // Everything stays framed (the dock settling, an import landing) until the user first works the
    // graph: reframed whenever the panel's size or the items change.
    bool        m_autoFrame = true;
    ImVec2      m_framedSize;
    uint64_t    m_framedItems = 0;

    GraphScene             m_scene;
    std::vector<LinkCurve> m_curves;        // screen, one per scene link
    uint64_t m_curvesRevision = 0;          // the scene's links, as the curves were placed for
    std::array<float, 6> m_linkCameras{};   // both cameras, as the curves were placed for
    std::array<float, 9> m_minimapKey{};    // what the minimap was laid out for
    uint64_t m_minimapRevision = 0;         // the scene's items, as the minimap was laid out for
    GraphMenus             m_menus;   // the add menu, a node's menu, the value field

    // interaction
    Press  m_press;
    Drag   m_drag;
    bool   m_canvasActive = false;
    int    m_hoveredLink = -1;    // last frame's, for the insert button
    bool   m_rightDown = false;       // a right press on the panel is in progress
    bool   m_rightZooming = false;    // ... held or dragged: it zooms, no menu on release
    ImVec2 m_rightPress;              // where (screen): the zoom's anchor
    Deadline m_rightHold;             // ... long enough to zoom once it expires
};
