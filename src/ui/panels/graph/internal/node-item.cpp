#include "ui/panels/graph/internal/node-item.hpp"
#include "document/workspace.hpp"
#include "ui/panels/graph/internal/layout.hpp"
#include "ui/style/graph-style.hpp"

#include <algorithm>
#include <cstdint>

using namespace GraphLayout;
using namespace GraphStyle;

const NodeItemPort* NodeItem::output() const
{
    for (const NodeItemPort& p : ports) if (p.output) return &p;
    return nullptr;
}

const NodeItemPort* NodeItem::input(const InputSlot& slot) const
{
    for (const NodeItemPort& p : ports) if (!p.output && p.slot == slot) return &p;
    return nullptr;
}

std::optional<ImVec2> NodeItem::linkEnd(const InputSlot& slot) const
{
    if (const NodeItemPort* p = input(slot)) return p->pos;
    for (const LinkEnd& e : ends) if (e.slot == slot) return e.pos;
    return std::nullopt;
}

void NodeItem::moveTo(float x, float y)
{
    const ImVec2 d(x - header.Min.x, y - header.Min.y);   // the header's corner is the node's position
    const auto shift = [&](ImRect& r) { if (r.Min.x <= r.Max.x) r.Translate(d); };   // empty ones stay empty
    for (NodeItemBox& b : boxes) shift(b.rect);
    shift(header);
    if (ioRow) shift(*ioRow);
    if (thumb) shift(*thumb);
    for (NodeItemPropertyRow& r : properties) shift(r.rect);
    for (NodeItemPort& p : ports) p.pos = ImVec2(p.pos.x + d.x, p.pos.y + d.y);
    for (NodeItemWidget& w : widgets) shift(w.rect);
    for (NodeItemSectionRow& r : sections) shift(r.rect);
    for (NodeItemChannelRow& r : channels) { shift(r.rect); shift(r.track); }
    for (NodeItemLayerRow& r : layers) shift(r.rect);
    if (dropInto) shift(*dropInto);
    if (stackArea) shift(*stackArea);
    for (LinkEnd& e : ends) e.pos = ImVec2(e.pos.x + d.x, e.pos.y + d.y);
    shift(bounds);
}

namespace {

class Builder {
public:
    Builder(const Workspace& p, const Node& n, const NodeItemState& v, StackLayout stack, NodeItem& s)
        : m_workspace(p), m_node(n), m_view(v), m_stack(stack), m_item(s), m_x0(n.x()), m_x1(n.x() + kNodeWidth), m_y(n.y()) {}

    void build()
    {
        m_item.id = m_node.id();
        m_item.stack = m_stack;
        m_item.boxes.clear();
        m_item.ports.clear();
        m_item.widgets.clear();
        m_item.sections.clear();
        m_item.channels.clear();
        m_item.layers.clear();
        m_item.properties.clear();
        m_item.ioRow.reset();
        m_item.thumb.reset();
        m_item.dropInto.reset();
        m_item.stackArea.reset();
        m_item.origin.reset();
        m_item.ends.clear();
        const bool master = m_workspace.isMaster(m_item.id);
        const bool stack  = m_node.ports().inputs == InputShape::Stack;
        const NodeSettings settings = m_node.settings();

        // ---- header (+ body, or the master's stack)
        const float top = m_y;
        m_item.header = ImRect(m_x0, top, m_x1, top + kHeaderH);
        widget(TitleControl{}, m_item.header);
        m_y = m_item.header.Max.y;
        if (master) {
            layerList();
        } else {
            body(settings);
        }
        closeBox(top, kHeaderH);

        // ---- properties: its kind's settings, or its stack
        if (std::holds_alternative<MoveSettings>(settings)) {
            static const char* kLabel[2] = { "Offset X", "Offset Y" };
            const float boxTop = openBox();
            m_y += 4.0f;
            for (int axis = 0; axis < 2; ++axis) {
                m_item.properties.push_back({ ImRect(m_x0, m_y, m_x1, m_y + kRowH), kLabel[axis] });
                widget(OffsetControl{ axis }, ImRect(m_x0 + kPad + 56.0f, m_y + 2.0f, m_x1 - kPad, m_y + kRowH - 2.0f));
                m_y += kRowH;
            }
            m_y += 4.0f;
            closeBox(boxTop);
        } else if (stack && !master) {
            const float boxTop = openBox();
            layerList();
            closeBox(boxTop);
        }

        // ---- signals
        const float boxTop = openBox();
        m_y += 3.0f;
        section(NodeSignal::Main);
        if (m_node.hasDryWet()) {   // effect nodes only
            section(NodeSignal::Wet);
            section(NodeSignal::Dry);
        }
        m_y += 3.0f;
        closeBox(boxTop);

        m_item.bounds = m_item.boxes.front().rect;
        for (const NodeItemBox& b : m_item.boxes) m_item.bounds.Add(b.rect);
    }

private:
    void widget(NodeItemControl c, ImRect r) { m_item.widgets.push_back({ c, r }); }
    void inputPort(float y, InputSlot slot, bool mask = false, bool placeholder = false, float indent = 0.0f)
    {
        m_item.ports.push_back({ ImVec2(m_x0 + indent, y), false, slot, mask, placeholder });
    }

    float openBox() { m_y += kBoxGap; return m_y; }
    void  closeBox(float top, float header = 0.0f) { m_item.boxes.push_back({ ImRect(m_x0, top, m_x1, m_y), header }); }

    void body(const NodeSettings& settings)
    {
        const ImRect io(m_x0, m_y, m_x1, m_y + kIoRowH);
        m_item.ioRow = io;
        const float cy = io.GetCenter().y;
        m_item.ports.push_back({ ImVec2(m_x1, cy), true, {}, false, false });
        if (m_node.ports().inputs == InputShape::One) inputPort(cy, InputSlot::input());
        if (std::holds_alternative<RasterSettings>(settings)) {
            const float h = kIconSize * 0.5f;
            const float x = m_x0 + kPad;
            widget(LockControl{},      ImRect(x, cy - h, x + kIconSize, cy + h));
            widget(LockAlphaControl{}, ImRect(x + kIconSize + 6.0f, cy - h, x + 2.0f * kIconSize + 6.0f, cy + h));
        }
        const float tx = m_x0 + (kNodeWidth - kThumbW) * 0.5f;
        m_item.thumb = ImRect(tx, io.Max.y + 1.0f, tx + kThumbW, io.Max.y + 1.0f + kThumbH);
        m_y = m_item.thumb->Max.y + kThumbBelow;
    }

    void layerList()
    {
        // The stack's rows (the master's in groups, its artboards). The ones a drag carries keep
        // their spot, as a placeholder: nothing closes up while it goes on.
        const std::vector<NodePtr>& rows = m_node.links().layers;
        const std::vector<StackGroup> groups = stackSettings(m_node).groups;
        const auto has = [](const std::vector<StackRowRef>& v, StackRowRef r) { return std::find(v.begin(), v.end(), r) != v.end(); };

        // The top level, bottom first (StackGroups::entries, what moves rows too); carried ones marked.
        struct Member { uint32_t row; bool carried; };
        struct Entry { int row = -1; ArtboardId key = kNoArtboard; std::vector<Member> members; bool carried = false; };
        std::vector<Entry> entries;
        for (const StackGroups::Entry& e : StackGroups::entries(groups, rows.size())) {
            if (e.group == kNoGroup) {
                entries.push_back({ int(e.begin), kNoArtboard, {}, has(m_stack.lifted, { rows[e.begin]->id(), kNoArtboard }) });
                continue;
            }
            Entry g{ -1, e.group, {}, has(m_stack.lifted, { kNoNodeId, e.group }) };
            for (uint32_t r = e.begin; r < e.end; ++r) g.members.push_back({ r, has(m_stack.lifted, { rows[r]->id(), kNoArtboard }) });
            entries.push_back(std::move(g));
        }
        const auto left = [](const auto& v) {   // what isn't carried
            return size_t(std::count_if(v.begin(), v.end(), [](const auto& x) { return !x.carried; }));
        };

        // A drag over it: the room (a container and a position among the rows left; none: SIZE_MAX),
        // or the artboard whose row it's on.
        const StackGap& gap = m_stack.gap;
        const bool       roomShown = gap.opens();
        const ArtboardId gapInto   = roomShown ? gap.place().into : kNoArtboard;
        const size_t     gapAt     = roomShown ? gap.place().at : SIZE_MAX;
        const ArtboardId onto      = gap.artboard();
        const bool       single    = m_stack.lifted.size() == 1;

        const float areaTop = m_y;
        m_y += 4.0f;
        const float x0 = m_x0 + 6.0f, x1 = m_x1 - 6.0f;
        const auto indent = [](ArtboardId inside) { return inside != kNoArtboard ? kArtboardIndent : 0.0f; };
        const auto place = [&](float height, ArtboardId inside) {
            const ImRect r(x0 + indent(inside), m_y, x1, m_y + height);
            m_y = r.Max.y + kLayerGap;
            return r;
        };
        const auto gapRow = [&](ArtboardId inside) {
            const bool line = gap.target() == StackGap::Target::Line;
            const ImRect r = place(line ? kDropLineH : kRoomH, inside);
            m_item.layers.push_back({ r, NodeItemLayerRow::Gap{ line }, inside, false });
            if (gap.kind() == StackGap::Kind::Link)   // the link lands here
                inputPort(r.GetCenter().y, InputSlot::newLayer(uint32_t(rows.size())), false, true, indent(inside));
        };
        const auto originRow = [&](float height, ArtboardId inside, size_t at, ArtboardId artboard = kNoArtboard) {   // a carried row's spot
            m_item.layers.push_back({ place(height, inside), NodeItemLayerRow::Origin{ artboard }, inside, false });
            if (single && (artboard == kNoArtboard || inside == kNoArtboard)) m_item.origin = StackPlace{ inside, at };
        };
        const auto layerRow = [&](uint32_t r, ArtboardId inside) {
            const ImRect rect = place(kLayerRowH, inside);
            m_item.layers.push_back({ rect, NodeItemLayerRow::Layer{ r }, inside, selected({ rows[r]->id(), kNoArtboard }) });
            inputPort(rect.GetCenter().y, InputSlot::stackLayer(r), false, false, indent(inside));
            widget(LayerMuteControl{ r }, layerEyeRect(rect));   // before the row's own: it's inside it
            widget(LayerControl{ r }, rect);
        };

        if (entries.empty()) {
            // Where the first layer is dropped. A drop on it lands on it: it stays the size it is
            // (room in its place would shrink the stack from under the mouse).
            const ImRect r = place(kLayerRowH, kNoArtboard);
            m_item.layers.push_back({ r, NodeItemLayerRow::Empty{ roomShown }, kNoArtboard, false });
            if (roomShown && gap.kind() == StackGap::Kind::Link) inputPort(r.GetCenter().y, InputSlot::newLayer(0), false, true);
        } else {
            // Top first. `below`: the rows left under what comes next, so the room goes in where as
            // many are under it as its position says.
            size_t below = left(entries);
            bool roomDone = false;
            for (size_t i = entries.size(); i-- > 0;) {
                if (gapInto == kNoArtboard && !roomDone && below == gapAt) { gapRow(kNoArtboard); roomDone = true; }
                const Entry& e = entries[i];
                if (e.carried) {   // a layer's spot, or an artboard's: its row and its layers'
                    if (e.row >= 0) {
                        originRow(kLayerRowH, kNoArtboard, below);
                        continue;
                    }
                    originRow(kArtboardRowH, kNoArtboard, below, e.key);
                    if (std::find(m_view.folded.begin(), m_view.folded.end(), e.key) == m_view.folded.end())
                        for (size_t m = e.members.size(); m-- > 0;) originRow(kLayerRowH, e.key, 0, e.key);
                    continue;
                }
                --below;
                if (e.row >= 0) {
                    layerRow(uint32_t(e.row), kNoArtboard);
                    continue;
                }
                const bool folded = std::find(m_view.folded.begin(), m_view.folded.end(), e.key) != m_view.folded.end();
                const ImRect head = place(kArtboardRowH, kNoArtboard);
                m_item.layers.push_back({ head, NodeItemLayerRow::Artboard{ e.key, folded, uint32_t(left(e.members)) }, kNoArtboard,
                                          selected({ kNoNodeId, e.key }) });
                widget(ArtboardFoldControl{ e.key }, artboardFoldRect(head));
                widget(ArtboardMuteControl{ e.key }, artboardEyeRect(head));
                widget(ArtboardColorControl{ e.key }, artboardColorRect(head));
                widget(ArtboardControl{ e.key }, head);
                if (onto == e.key && gap.kind() == StackGap::Kind::Link)   // a link dropped on its row lands there
                    inputPort(head.GetCenter().y, InputSlot::newLayer(uint32_t(rows.size())), false, true);
                ImRect extent = head;
                if (folded) {
                    // No ports: its layers' links end at its row (nothing to grab there).
                    for (const Member& m : e.members)
                        if (!m.carried) m_item.ends.push_back({ InputSlot::stackLayer(m.row), ImVec2(m_x0, head.GetCenter().y) });
                    if (gapInto == e.key) gapRow(e.key);   // folded while the room was in it
                } else {
                    size_t in = left(e.members);
                    bool done = false;
                    for (size_t m = e.members.size(); m-- > 0;) {
                        if (gapInto == e.key && !done && in == gapAt) { gapRow(e.key); done = true; }
                        if (e.members[m].carried) {
                            originRow(kLayerRowH, e.key, in);
                            continue;
                        }
                        --in;
                        layerRow(e.members[m].row, e.key);
                    }
                    if (gapInto == e.key && !done && in == gapAt) gapRow(e.key);
                }
                extent.Max.y = std::max(extent.Max.y, m_y - kLayerGap);
                if (onto == e.key || gapInto == e.key) m_item.dropInto = extent;   // outlined: "into this one"
            }
            if (gapInto == kNoArtboard && !roomDone && below == gapAt) gapRow(kNoArtboard);
        }
        m_y += 4.0f - kLayerGap;
        m_item.stackArea = ImRect(m_x0, areaTop, m_x1, m_y);
    }

    bool selected(StackRowRef r) const
    {
        return std::find(m_view.selected.begin(), m_view.selected.end(), r) != m_view.selected.end();
    }

    void section(NodeSignal sig)
    {
        const bool expanded = m_view.expanded[int(sig)];
        const ImRect head(m_x0, m_y, m_x1, m_y + kRowH);
        m_item.sections.push_back({ head, sig, expanded });
        widget(SectionControl{ sig }, ImRect(m_x0 + 4.0f, head.Min.y, m_x1 - 4.0f, head.Max.y));
        m_y = head.Max.y;

        for (int ch = 0; ch < kChannelCount; ++ch) {
            // Collapsed: alpha, plus any channel with a mask (a link never ends nowhere).
            if (!expanded && ch != int(NodeChannel::A) && !m_node.mask(sig, NodeChannel(ch))) continue;
            const ImRect row(m_x0, m_y, m_x1, m_y + kRowH);
            const ImRect track(m_x0 + 16.0f, row.Min.y + 3.0f, m_x1 - 50.0f, row.Max.y - 3.0f);
            m_item.channels.push_back({ row, track, sig, NodeChannel(ch) });
            // The whole row past the port: the track drags, the value double-clicks to type.
            widget(ScaleControl{ sig, NodeChannel(ch) }, ImRect(track.Min.x - 5.0f, row.Min.y, row.Max.x - 2.0f, row.Max.y));
            inputPort(row.GetCenter().y, InputSlot::mask(sig, NodeChannel(ch)), true);
            m_y = row.Max.y;
        }
    }

    const Workspace&       m_workspace;
    const Node&          m_node;
    const NodeItemState& m_view;
    const StackLayout    m_stack;
    NodeItem&            m_item;
    const float m_x0, m_x1;
    float m_y;
};

}

ImRect layerEyeRect(const ImRect& r)
{
    const float cy = r.GetCenter().y, h = kIconSize * 0.5f;
    return ImRect(r.Min.x + 6.0f, cy - h, r.Min.x + 6.0f + kIconSize, cy + h);
}

ImRect layerThumbRect(const ImRect& r)
{
    const float th = r.GetHeight() - 6.0f;
    const float x = layerEyeRect(r).Max.x + 5.0f;
    return ImRect(x, r.Min.y + 3.0f, x + th * 4.0f / 3.0f, r.Max.y - 3.0f);
}

ImRect artboardFoldRect(const ImRect& r)
{
    const float cy = r.GetCenter().y, h = kIconSize * 0.5f;
    return ImRect(r.Min.x + 3.0f, cy - h, r.Min.x + 3.0f + kIconSize, cy + h);
}

ImRect artboardEyeRect(const ImRect& r)
{
    const ImRect fold = artboardFoldRect(r);
    return ImRect(fold.Max.x + 4.0f, fold.Min.y, fold.Max.x + 4.0f + kIconSize, fold.Max.y);
}

ImRect artboardColorRect(const ImRect& r)
{
    const ImRect eye = artboardEyeRect(r);
    return ImRect(eye.Max.x + 6.0f, eye.Min.y, eye.Max.x + 6.0f + kIconSize, eye.Max.y);
}

void buildNodeItem(const Workspace& workspace, const Node& n, const NodeItemState& view, StackLayout stack, NodeItem& out)
{
    Builder(workspace, n, view, n.ports().inputs == InputShape::Stack ? stack : StackLayout{}, out).build();
}
