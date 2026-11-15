#include "ui/panels/graph/internal/link-drop.hpp"
#include "ui/panels/graph/internal/layout.hpp"

LinkZone linkZoneAt(const NodeItem& item, ImVec2 p)
{
    if (item.stackArea) {
        ImRect area = *item.stackArea;
        area.Expand(ImVec2(0.0f, GraphLayout::kStackBleed));
        if (area.Contains(p)) return { LinkZone::Kind::Stack };
    }
    for (const NodeItemChannelRow& row : item.channels)
        if (row.rect.Contains(p)) return { LinkZone::Kind::Channel, &row };
    if (!item.boxes.empty() && item.boxes.front().rect.Contains(p)) return { LinkZone::Kind::Body };
    return {};
}

LinkLanding linkLanding(const NodeItem& item, const Node& n, ImVec2 at, const NodeItemPort* port, const LinkEnd& end,
                        const std::function<bool(const InputSlot&)>& fits,
                        const std::function<StackGap(ImVec2, const std::vector<StackRowRef>&)>& place)
{
    if (!end.fromOutput) {   // looking for an output: its port
        if (port && port->output && fits({})) return { true, {}, {}, port->pos };
        return {};
    }
    const LinkZone zone = linkZoneAt(item, at);
    // Over a stack's rows: into the stack, at a place, as rows are dropped (it lands on top, then
    // moves there; a lifted one comes off its own row first).
    if (n.ports().inputs == InputShape::Stack && zone.kind == LinkZone::Kind::Stack && !(port && port->mask)) {
        const bool own = end.liftedFrom == n.id();
        const InputSlot slot = InputSlot::newLayer(own ? 0u : uint32_t(n.links().layers.size()));
        if (!fits(slot)) return {};
        // A link lifted off this stack's own row: that row is carried (its spot kept).
        const std::vector<StackRowRef> carried = own ? std::vector<StackRowRef>{ { end.liftedSource, kNoArtboard } } : std::vector<StackRowRef>{};
        LinkLanding l{ true, slot, place(at, carried), at };
        if (!l.place.dropping()) return {};   // the stack isn't laid out for it yet
        for (const NodeItemPort& p : item.ports)   // the port at the gap, once it is open
            if (p.placeholder && p.slot.is<InputSlot::NewLayer>()) l.pos = p.pos;
        return l;
    }
    if (port && !port->output) {   // on an input's port: that input
        if (fits(port->slot)) return { true, port->slot, {}, port->pos };
        return {};
    }
    if (zone.kind == LinkZone::Kind::Channel) {   // that channel's mask
        const InputSlot slot = InputSlot::mask(zone.channel->signal, zone.channel->channel);
        if (const NodeItemPort* p = item.input(slot); p && fits(slot)) return { true, slot, {}, p->pos };
        return {};
    }
    // A one-input node's body: its input. Anywhere else on a node: nothing.
    if (n.ports().inputs == InputShape::One && zone.kind == LinkZone::Kind::Body)
        if (const NodeItemPort* p = item.input(InputSlot::input()); p && fits(InputSlot::input()))
            return { true, InputSlot::input(), {}, p->pos };
    return {};
}
