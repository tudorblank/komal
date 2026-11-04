#pragma once
// A drag over a stack (a link, or rows) and where it drops. The stack is an array of rows, top
// first: layers and artboards, each artboard one row of it taking its own space; an open artboard
// holds its own array of layers under its row. A drop goes to one of these (StackGap::Target):
//   between rows     room opens there, the same room anywhere (in the top level, or in an open
//                    artboard's array: a StackPlace); in an artboard, it's outlined around it.
//                    The very bottom of the stack, under an artboard, shows a thick line instead
//   onto an artboard its row takes it (into it, on top of its layers). Nothing opens or moves:
//                    the room it came from stays where it was, as empty space, and the artboard
//                    is outlined
//   back home        the rows a drag carries keep their spot, outlined dashed (nothing closes up
//                    while it goes on); over one row's spot, or a place next to it (the same
//                    place), nothing opens: dropped there, it stays
// Moving to the next place between rows swaps the room with one row, so the mouse is over the
// room again; onto an artboard moves nothing. Either way the drop never jumps by itself.
//
// The graph lays a stack out with the drag (StackLayout: buildNodeItem), works out the drop on
// that layout (stackDrop, settleDrop), and lands it (landing) as a layers.move / layer.add place.

#include "document/artboard.hpp"
#include "node/node.hpp"

#include <cstdint>
#include <functional>
#include <vector>

struct NodeItem;

// A stack's row as the graph names it: a layer by its node, or one of the master's artboards.
struct StackRowRef {
    NodeId     layer = kNoNodeId;
    ArtboardId artboard = kNoArtboard;
    bool empty() const { return layer == kNoNodeId && artboard == kNoArtboard; }
    bool operator==(const StackRowRef&) const = default;
};

// A place in a stack: a position among the rows left in an array (StackMove's into + at).
struct StackPlace {
    ArtboardId into = kNoArtboard;   // in this artboard (none: the top level) ...
    size_t     at = 0;               // ... at this position among the rows left there (0 = bottom)
    bool operator==(const StackPlace&) const = default;
};

// The drop a drag shows: made only by its factories, so it's always one target, whole.
class StackGap {
public:
    enum class Kind : uint8_t { None, Link, Rows };   // what's dragged (None: nothing, no drop)
    enum class Target : uint8_t {
        Room,   // room opens at place(): the drop lands there
        Line,   // ... the same, shown as a thick line (the very bottom, under an artboard)
        Home,   // place() is where the one row carried was: nothing opens; dropped, it stays
        Onto,   // artboard()'s row: into it, on top of its layers; nothing opens. Holds hold() under
                // its row: as far as the rows moved up when it took the drop (a room above it closing)
    };

    StackGap() = default;   // no drag
    static StackGap room(Kind kind, StackPlace place, bool line = false) { return { kind, line ? Target::Line : Target::Room, place, 0.0f }; }
    static StackGap home(Kind kind, StackPlace place) { return { kind, Target::Home, place, 0.0f }; }
    static StackGap onto(Kind kind, ArtboardId artboard, float hold = 0.0f) { return { kind, Target::Onto, { artboard, 0 }, hold }; }
    StackGap as(Kind kind) const { return { kind, m_target, m_place, m_hold }; }   // the same drop, for this drag

    Kind   kind() const { return m_kind; }
    bool   dropping() const { return m_kind != Kind::None; }
    Target target() const { return m_target; }
    bool   opens() const { return dropping() && (m_target == Target::Room || m_target == Target::Line); }   // room shown
    StackPlace place() const { return m_target == Target::Onto ? StackPlace{} : m_place; }               // Room, Line, Home
    ArtboardId artboard() const { return dropping() && m_target == Target::Onto ? m_place.into : kNoArtboard; }   // Onto
    float      hold() const { return m_target == Target::Onto ? m_hold : 0.0f; }
    bool operator==(const StackGap&) const = default;

private:
    StackGap(Kind kind, Target target, StackPlace place, float hold) : m_kind(kind), m_target(target), m_place(place), m_hold(hold) {}
    Kind       m_kind = Kind::None;
    Target     m_target = Target::Room;
    StackPlace m_place;          // Onto: the artboard in `into`
    float      m_hold = 0.0f;    // Onto only
};

// A stack laid out around a drag: the rows it carries keep their spot, the drop shows.
struct StackLayout {
    StackGap gap;
    std::vector<StackRowRef> lifted;
    bool operator==(const StackLayout&) const = default;
};

// The drop at graph height y on a stack, from its rows as laid out with the drag so far:
//   a layer's row     upper half: room above it; lower half: under it (in its array: an artboard's
//                     own, if it's in one)
//   an artboard's row onto it; under another artboard (its row or its layers), its top edge
//                     (kArtboardEdge) is room above it instead: between the two, outside both.
//                     Taking the drop closes any room above it: the rows move up, so onto a
//                     folded or empty one the drop holds that far under its row (StackGap::hold),
//                     where it was under the mouse before. Nothing closed: no hold, the row
//                     under it gives its own places as anywhere else
//   the room          the room's place again
//   a carried row's spot: where it was
//   above every row: the top; below every row: the very bottom, out of any artboard
// Carrying an artboard, nothing goes into one: its row's upper half is above it, its lower half
// (and its layers) under it. `carried`: the rows the drag carries; the item must be laid out with
// them (their spots kept), else no drop yet: one worked out on a stack that doesn't show them yet
// would be a place in another stack.
StackGap stackDrop(const NodeItem& item, float y, StackGap::Kind kind, bool carriesArtboard,
                   const std::vector<StackRowRef>& carried = {});
// The drop at height y settled: worked out again on the stack laid out with it (layoutOf) until it
// gives itself back, so what shows is what the mouse is on (closing a room moves the rows under it).
StackGap settleDrop(StackGap drop, const std::function<NodeItem(const StackGap&)>& layoutOf, float y, StackGap::Kind kind,
                    bool carriesArtboard, const std::vector<StackRowRef>& carried);
// Where a drop lands: onto an artboard, on top of its layers; home, where the row was; else the room's place.
StackPlace landing(const NodeItem& item, const StackGap& gap);
