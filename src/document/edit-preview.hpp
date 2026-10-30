#pragma once
// An edit in progress: what the user is still doing, before it becomes one real change. While it
// lasts the document is untouched; the views show it (the pixels on the GPU, canvas/preview; the
// graph's own items in the graph). Each kind holds everything it needs, its target included:
//
//   ScaleEdit       a signal's scale being dragged          applied: Project::applyPreview()
//   OffsetEdit      a Move's offset being dragged           applied: Project::applyPreview()
//   StrokeEdit      a brush stroke being painted            applied: Project::endStroke()
//   NodesMoveEdit   nodes being dragged in the graph        applied: Project::applyPreview()
//   LinkMoveEdit    a link lifted off an input, carried     applied: by link.move, where it's dropped
//
// No edit in progress is no EditPreview (Project::preview() is null).

#include "document/input-slot.hpp"

#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

struct ScaleEdit {
    NodeId      node;
    NodeSignal  signal;
    NodeChannel channel;
    float       scale;   // 0..1
    bool operator==(const ScaleEdit&) const = default;
};

struct OffsetEdit {
    NodeId node;   // a Move
    int    x, y;   // in the world (TileUtils::inWorld)
    bool operator==(const OffsetEdit&) const = default;
};

// The stroke being painted into `raster`: its coverage and brush are the stroke session's
// (Project::stroke()); the revision tells them apart as it grows.
struct StrokeEdit {
    NodeId   raster;
    uint64_t revision;   // StrokeSession::revision() when it last stamped
    bool operator==(const StrokeEdit&) const = default;
};

struct NodesMoveEdit {
    std::vector<NodeId> nodes;
    float dx, dy;        // graph units, from where they are
    bool operator==(const NodesMoveEdit&) const = default;
};

// The link feeding `slot` of `from` (from `source`), lifted: the graph shows it carried.
struct LinkMoveEdit {
    NodeId    source, from;
    InputSlot slot;
    bool operator==(const LinkMoveEdit&) const = default;
};

struct EditPreview {
    using Edit = std::variant<ScaleEdit, OffsetEdit, StrokeEdit, NodesMoveEdit, LinkMoveEdit>;

    Edit edit;

    EditPreview() = default;
    template <class T> requires std::is_constructible_v<Edit, T>
    EditPreview(T e) : edit(std::move(e)) {}   // from any one of them

    template <class T> const T* as() const { return std::get_if<T>(&edit); }
    const StrokeEdit* stroke() const { return as<StrokeEdit>(); }

    // The node whose output it changes (its pixels, and everything downstream's); none for an
    // edit of the graph alone (a node or link being moved).
    NodeId pixelsNode() const
    {
        if (const auto* s = as<ScaleEdit>())  return s->node;
        if (const auto* o = as<OffsetEdit>()) return o->node;
        if (const auto* k = as<StrokeEdit>()) return k->raster;
        return kNoNodeId;
    }
    // The scale it shows for that channel of that node, if that's what is being dragged.
    std::optional<float> scaleOf(NodeId n, NodeSignal s, NodeChannel c) const
    {
        const ScaleEdit* e = as<ScaleEdit>();
        if (!e || e->node != n || e->signal != s || e->channel != c) return std::nullopt;
        return e->scale;
    }
    // The offset it shows for that Move, if it's the one being dragged.
    const OffsetEdit* offsetOf(NodeId n) const
    {
        const OffsetEdit* e = as<OffsetEdit>();
        return e && e->node == n ? e : nullptr;
    }
    bool operator==(const EditPreview&) const = default;
};
