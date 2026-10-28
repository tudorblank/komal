#pragma once

#include "graph/camera.hpp"
#include "graph/minimap.hpp"
#include "graph/selection.hpp"
#include "graph/node-menu.hpp"
#include "graph/painter.hpp"
#include "graph/node-item/item.hpp"
#include "graph/node-item/thumbnails.hpp"

#include <absl/container/flat_hash_map.h>
#include <array>
#include <optional>
#include <vector>

class ActionRegistry;
class GpuDevice;
struct InputFrame;
class Project;
struct ProjectEvent;

// The node graph editor. Like a Qt scene, it keeps an item per node and per link, built once
// and then changed only by what Project says changed:
//
//   NodeAdded      addNodeItem      item laid out, links into it added
//   NodeRemoved    removeNodeItem   item and its links dropped
//   NodeMoved      moveNodeItem     item shifted, its links' ends follow
//   LinkAdded      addLink          one link (a mask link also lays out its node: a row appears)
//   LinkRemoved    removeLink       one link
//   LayersChanged  layoutNodeItem   the stack laid out again, its layer links re-added
//   anything else                   nothing: values and pixels are painted as they are
//
// Edits go the other way: input calls Project (connect, setNodePosition, ...), and the event it
// emits updates the items. The graph's own view state works the same way: opening a section or
// a drag opening a gap in a stack lays out that one item. Link curves are screen space, placed
// again when a link or a camera moved. A frame paints and hit-tests the items as they are.
class GraphPanel {
public:
    void init(GpuDevice& gpu, Project& project, const InputFrame& input, ActionRegistry& actions);
    void shutdown();
    void setUiScale(float scale) { m_uiScale = scale; }
    void draw(bool* open);
    // A file dropped at `screen` (`window` topmost): true if it lands on the graph, with the
    // position for a new node centred under it.
    bool dropTarget(const ImGuiWindow* window, ImVec2 screen, ImVec2& nodePos) const;
    void selectOnly(NodeId id);
    const GraphSelection::Set& selection() const { return m_selection.ids(); }
    double nextFrameIn() const;   // see Application::nextFrameIn
    // A world rectangle for the canvas to show (double-click, "Show in Canvas"). True once per request.
    bool takeCanvasFrame(BoundsI& out);

private:
    static constexpr float       kPinMargin   = 12.0f;   // points between the pinned master and the panel's edges
    static constexpr const char* kNodeContext = "##node-context";
    static constexpr const char* kValueEdit   = "##value-edit";

    struct Hit {
        enum class Kind : uint8_t { None, Port, Widget, Node, Link, Insert, Minimap };
        Kind   kind  = Kind::None;
        NodeId node  = kNoNodeId;
        int    index = -1;   // port or widget index in the node item; link index
    };
    enum class Drag : uint8_t { None, Node, MoveNodes, Empty, RubberBand, Link, Scale, Offset, Layer, Minimap };

    struct GraphLink {
        NodeId    from, to;
        InputSlot slot;
        bool      mask;
        bool      toMaster;           // drawn dashed, fading out
        LinkCurve curve;              // screen
        ImVec2    fromGraph, toGraph; // port positions in each node's own space
        bool      shown = true;       // both ports are there (a lifted layer's isn't)
    };
    struct LinkDrag {
        NodeId    node = kNoNodeId;   // the end that stays put
        bool      fromOutput = true;  // that end is an output: looking for an input
        InputSlot slot;               // ... or that input
        bool      pickedUp = false;   // lifted off an input
    };
    struct LinkTarget {
        bool      valid = false;
        NodeId    node = kNoNodeId;
        InputSlot slot;
        ImVec2    pos;                // screen
    };
    struct PendingAdd {
        enum class Mode : uint8_t { Free, FromOutput, ToInput, Insert };
        Mode      mode = Mode::Free;
        ImVec2    at;                 // graph
        NodeId    from = kNoNodeId, to = kNoNodeId;
        InputSlot slot;
    };
    struct ValueEdit {
        enum class Kind : uint8_t { Scale, Offset, Rename };
        Kind        kind = Kind::Scale;
        NodeId      node = kNoNodeId;
        NodeSignal  signal  = NodeSignal::Main;
        NodeChannel channel = NodeChannel::A;
        int         axis = 0;
        float       value = 0.0f;
        char        text[128] = "";
        bool        justOpened = false;
    };

    GraphPaint paintContext(ImDrawList* dl) const { return { dl, m_camera, m_uiScale }; }
    bool isMaster(NodeId id) const;
    const GraphCamera& cameraFor(NodeId id) const { return isMaster(id) ? m_pinned : m_camera; }   // whose space its node item is in
    // ---- items, kept in step with the project
    void onProjectEvent(const ProjectEvent& e);
    void addNodeItem(NodeId id, bool withLinks = true);
    void removeNodeItem(NodeId id);
    void moveNodeItem(NodeId id);
    void layoutNodeItem(NodeId id);   // after anything its layout depends on changed
    void addLink(NodeId from, NodeId to, const InputSlot& slot);
    void removeLink(NodeId to, const InputSlot& slot);
    void addLinksInto(NodeId to, bool layersOnly = false);   // what the project wires into it
    void placeLinkEnds(GraphLink& l) const;
    void placeLinkEndsOf(NodeId id);   // its ports moved
    StackLayout stackLayout(NodeId id) const;   // the gap a drag opens in its stack, if any
    void updateStackGaps();           // a drag opened, moved or closed one
    void layoutOverlays(ImVec2 canvasMin, ImVec2 canvasMax);   // the pinned master and the navigator
    Hit  hitNode(NodeId id, const NodeItem& s, ImVec2 graphPoint, float portRadius) const;
    void placeLinkCurves(const GraphPaint& g);   // when a link or a camera moved
    Hit  hitTest(ImVec2 mouse, const GraphPaint& g) const;
    const NodeItem* item(NodeId id) const;
    void bringToFront(NodeId id);

    void handleMouse(const Hit& hit, bool hovered);
    void registerActions(ActionRegistry& actions);
    void pressNode(NodeId id);
    void pressWidget(const Hit& hit, bool doubleClick);
    void startLink(const Hit& hit);
    void updateDrag();
    LinkTarget linkTarget(const Hit& hit) const;
    uint32_t   stackPosition(NodeId id, float y) const;   // where in its stack something dropped at graph y goes
    void finishLink(const Hit& hit);
    void openAdd(const PendingAdd& add, NodeMenu::Fit fit);
    void applyAdd(NodeType type);
    ImVec2 insertPoint(const GraphLink& l, ImVec2 mouse) const;   // screen
    void openValueEdit(const ValueEdit& edit);
    void deleteSelection();
    void frameNodes(bool selectionOnly, ImVec2 canvasSize);
    void showInCanvas(const std::vector<NodeId>& ids);   // their output's world bounds, for the canvas
    int  activeWidget(NodeId id) const;   // index of the widget being dragged, or -1

    void paint(const GraphPaint& g, const Hit& hit, ImVec2 canvasMin, ImVec2 canvasMax);
    void paintGrid(const GraphPaint& g, ImVec2 canvasMin, ImVec2 canvasMax) const;
    void drawPopups();

    Project*    m_project = nullptr;
    const InputFrame* m_input = nullptr;   // keys and modifiers, straight from SDL
    ActionRegistry*   m_actions = nullptr; // graph.* shortcuts: claimed for the graph while it has the pointer or focus
    int         m_subscription = 0;
    const ImGuiWindow* m_window = nullptr;   // this frame's, while the graph is shown
    ImVec2      m_canvasMin{};
    GraphCamera m_camera;
    GraphCamera m_pinned;              // the master's space: 100%, its item in the top-right corner
    float       m_pinnedScroll = 0.0f; // screen pixels, when the master is taller than the panel
    ImRect      m_pinnedRect;          // the master on screen
    ImVec2      m_canvasSize;
    GraphMinimap m_minimap;
    std::vector<GraphMinimap::Edge> m_minimapEdges;   // built with the links
    float       m_uiScale = 1.0f;
    int         m_frameOnStart = 3;   // frames to frame everything for (the dock layout settles first)

    absl::flat_hash_map<NodeId, NodeItemState> m_viewStates;
    absl::flat_hash_map<NodeId, NodeItem>      m_items;
    std::vector<NodeId>    m_zOrder;                       // back to front
    std::vector<NodeId>    m_gapped;                       // stacks laid out with a drag's gap
    std::vector<GraphLink> m_links;
    bool   m_linksMoved = true;             // curves (and the minimap's edges) to place again
    std::array<float, 6> m_linkCameras{};   // both cameras, as the curves were placed for
    uint64_t m_itemsVersion = 0;            // bumped whenever an item changes
    std::array<float, 9> m_minimapKey{};    // what the minimap was laid out for
    uint64_t m_minimapVersion = ~0ull;
    GraphSelection         m_selection;
    NodeItemThumbnails         m_thumbs;
    NodeMenu               m_menu;

    // interaction
    Drag   m_drag = Drag::None;
    Hit    m_press;
    ImVec2 m_pressMouse;          // screen
    ImVec2 m_pressGraph;
    bool   m_pressWasSelected = false;
    bool   m_pressCtrl = false, m_pressShift = false;
    NodeItemWidget m_pressWidget{};
    bool   m_canvasActive = false;
    std::vector<std::pair<NodeId, ImVec2>> m_moveStart;
    GraphSelection::Set m_bandBase;
    int    m_dragValue = 0;       // Offset: value at press; Layer: the row's current stack index
    LinkDrag m_link;
    struct { NodeId node = kNoNodeId; int at = -1; } m_pendingLayer;   // the gap open under a dragged link
    struct LayerDrag {
        NodeId   node   = kNoNodeId;   // the compositor
        uint32_t layer  = 0;           // stack index of the row being dragged
        float    grab   = 0.0f;        // mouse below the row's top, graph units
        bool     lifted = false;       // past the drag threshold: floats, with a gap where it lands
        int      at     = -1;          // that gap: a position among the other layers
    } m_layerDrag;
    int    m_hoveredLink = -1;    // last frame's, for the insert button
    std::optional<BoundsI> m_canvasFrame;   // see takeCanvasFrame()
    bool   m_rightDown = false;       // a right press on the panel is in progress
    bool   m_rightZooming = false;    // ... held or dragged: it zooms, no menu on release
    ImVec2 m_rightPress;              // where (screen): the zoom's anchor
    double m_rightPressAt = 0.0;
    NodeId m_contextNode = kNoNodeId;
    PendingAdd m_pendingAdd;
    ValueEdit  m_edit;
    bool       m_editPending = false;    // open the value popup at window level next
};
