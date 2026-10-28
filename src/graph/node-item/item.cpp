#include "graph/node-item/item.hpp"
#include "graph/style.hpp"

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

void NodeItem::moveTo(float x, float y)
{
    const ImVec2 d(x - header.Min.x, y - header.Min.y);   // the header's corner is the node's position
    const auto shift = [&](ImRect& r) { if (r.Min.x <= r.Max.x) r.Translate(d); };   // empty ones stay empty
    for (NodeItemBox& b : boxes) shift(b.rect);
    shift(header);
    shift(ioRow);
    shift(thumb);
    shift(offsetRow[0]);
    shift(offsetRow[1]);
    for (NodeItemPort& p : ports) p.pos = ImVec2(p.pos.x + d.x, p.pos.y + d.y);
    for (NodeItemWidget& w : widgets) shift(w.rect);
    for (NodeItemSectionRow& r : sections) shift(r.rect);
    for (NodeItemChannelRow& r : channels) { shift(r.rect); shift(r.track); }
    for (NodeItemLayerRow& r : layers) shift(r.rect);
    shift(bounds);
}

bool preferredInput(const Node& n, InputSlot& out)
{
    if (dynamic_cast<const UnaryNode*>(&n)) { out = InputSlot::input(); return true; }
    if (auto* c = dynamic_cast<const CompositorNode*>(&n)) { out = InputSlot::newLayer(uint32_t(c->layers().size())); return true; }
    return false;
}

namespace {

class Builder {
public:
    Builder(const Project& p, const Node& n, const NodeItemState& v, StackLayout stack, NodeItem& s)
        : m_project(p), m_node(n), m_view(v), m_stack(stack), m_item(s), m_x0(n.m_meta.x), m_x1(n.m_meta.x + kNodeWidth), m_y(n.m_meta.y) {}

    void build()
    {
        m_item.id = m_node.m_meta.id;
        m_item.stack = m_stack;
        m_item.boxes.clear();
        m_item.ports.clear();
        m_item.widgets.clear();
        m_item.sections.clear();
        m_item.channels.clear();
        m_item.layers.clear();
        m_item.ioRow = m_item.thumb = m_item.offsetRow[0] = m_item.offsetRow[1] = ImRect();
        const bool master = m_project.isMaster(m_item.id);
        auto* stack = dynamic_cast<const CompositorNode*>(&m_node);

        // ---- header (+ body, or the master's stack)
        const float top = m_y;
        m_item.header = ImRect(m_x0, top, m_x1, top + kHeaderH);
        widget(NodeItemControl::Title, m_item.header);
        m_y = m_item.header.Max.y;
        if (master) {
            layerList(*stack);
        } else {
            body();
        }
        closeBox(top, kHeaderH);

        // ---- properties
        if (dynamic_cast<const MoveNode*>(&m_node)) {
            const float boxTop = openBox();
            m_y += 4.0f;
            for (int axis = 0; axis < 2; ++axis) {
                m_item.offsetRow[axis] = ImRect(m_x0, m_y, m_x1, m_y + kRowH);
                widget(axis == 0 ? NodeItemControl::OffsetX : NodeItemControl::OffsetY,
                       ImRect(m_x0 + kPad + 56.0f, m_y + 2.0f, m_x1 - kPad, m_y + kRowH - 2.0f));
                m_y += kRowH;
            }
            m_y += 4.0f;
            closeBox(boxTop);
        } else if (stack && !master) {
            const float boxTop = openBox();
            layerList(*stack);
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
    void widget(NodeItemControl c, ImRect r, NodeSignal sig = NodeSignal::Main, NodeChannel ch = NodeChannel::A, uint32_t layer = 0)
    {
        m_item.widgets.push_back({ c, r, sig, ch, layer });
    }
    void inputPort(float y, InputSlot slot, bool mask = false, bool placeholder = false)
    {
        m_item.ports.push_back({ ImVec2(m_x0, y), false, slot, mask, placeholder });
    }

    float openBox() { m_y += kBoxGap; return m_y; }
    void  closeBox(float top, float header = 0.0f) { m_item.boxes.push_back({ ImRect(m_x0, top, m_x1, m_y), header }); }

    void body()
    {
        m_item.ioRow = ImRect(m_x0, m_y, m_x1, m_y + kIoRowH);
        const float cy = m_item.ioRow.GetCenter().y;
        m_item.ports.push_back({ ImVec2(m_x1, cy), true, {}, false, false });
        if (dynamic_cast<const UnaryNode*>(&m_node)) inputPort(cy, InputSlot::input());
        if (m_node.m_meta.type == NodeType::Raster) {
            const float h = kIconSize * 0.5f;
            const float x = m_x0 + kPad;
            widget(NodeItemControl::Lock,        ImRect(x, cy - h, x + kIconSize, cy + h));
            widget(NodeItemControl::LockAlpha, ImRect(x + kIconSize + 6.0f, cy - h, x + 2.0f * kIconSize + 6.0f, cy + h));
        }
        const float tx = m_x0 + (kNodeWidth - kThumbW) * 0.5f;
        m_item.thumb = ImRect(tx, m_item.ioRow.Max.y + 1.0f, tx + kThumbW, m_item.ioRow.Max.y + 1.0f + kThumbH);
        m_y = m_item.thumb.Max.y + kThumbBelow;
    }

    void layerList(const CompositorNode& c)
    {
        // The layers shown, bottom first: all but the one being dragged.
        std::vector<uint32_t> shown;
        for (uint32_t i = 0; i < uint32_t(c.layers().size()); ++i)
            if (int(i) != m_stack.hidden) shown.push_back(i);

        m_y += 4.0f;
        const float x0 = m_x0 + 6.0f, x1 = m_x1 - 6.0f;
        const auto row = [&](NodeItemLayerRow::Kind kind, uint32_t layer) {
            const ImRect r(x0, m_y, x1, m_y + kLayerRowH);
            m_item.layers.push_back({ r, layer, kind });
            m_y = r.Max.y + kLayerGap;
            return r;
        };
        if (shown.empty() && m_stack.gap < 0) {
            row(NodeItemLayerRow::Kind::Empty, 0);   // where the first layer is dropped
        } else {
            // Top of the stack first; the gap goes in at its position, the rows below it move down.
            for (size_t pos = shown.size() + 1; pos-- > 0;) {
                if (int(pos) == m_stack.gap) {
                    const ImRect r = row(NodeItemLayerRow::Kind::Gap, uint32_t(pos));
                    if (m_stack.hidden < 0) inputPort(r.GetCenter().y, InputSlot::newLayer(uint32_t(pos)));   // a link lands here
                }
                if (pos == 0) break;
                const uint32_t i = shown[pos - 1];
                const ImRect r = row(NodeItemLayerRow::Kind::Layer, i);
                inputPort(r.GetCenter().y, InputSlot::stackLayer(i));
                widget(NodeItemControl::Layer, r, NodeSignal::Main, NodeChannel::A, i);
            }
        }
        m_y += 4.0f - kLayerGap;
    }

    void section(NodeSignal sig)
    {
        const bool expanded = m_view.expanded[int(sig)];
        const ImRect head(m_x0, m_y, m_x1, m_y + kRowH);
        m_item.sections.push_back({ head, sig, expanded });
        widget(NodeItemControl::SectionToggle, ImRect(m_x0 + 4.0f, head.Min.y, m_x1 - 4.0f, head.Max.y), sig);
        m_y = head.Max.y;

        const SignalChannels& sc = m_node.signal(sig);
        for (int ch = 0; ch < kChannelCount; ++ch) {
            // Collapsed: alpha, plus any channel with a mask (a link never ends nowhere).
            if (!expanded && ch != int(NodeChannel::A) && !sc.mask[ch]) continue;
            const ImRect row(m_x0, m_y, m_x1, m_y + kRowH);
            const ImRect track(m_x0 + 16.0f, row.Min.y + 3.0f, m_x1 - 50.0f, row.Max.y - 3.0f);
            m_item.channels.push_back({ row, track, sig, NodeChannel(ch) });
            // The whole row past the port: the track drags, the value double-clicks to type.
            widget(NodeItemControl::Scale, ImRect(track.Min.x - 5.0f, row.Min.y, row.Max.x - 2.0f, row.Max.y), sig, NodeChannel(ch));
            inputPort(row.GetCenter().y, InputSlot::mask(sig, NodeChannel(ch)), true);
            m_y = row.Max.y;
        }
    }

    const Project&       m_project;
    const Node&          m_node;
    const NodeItemState& m_view;
    const StackLayout    m_stack;
    NodeItem&            m_item;
    const float m_x0, m_x1;
    float m_y;
};

}

ImRect layerThumbRect(const ImRect& r)
{
    const float th = r.GetHeight() - 6.0f;
    return ImRect(r.Min.x + 3.0f, r.Min.y + 3.0f, r.Min.x + 3.0f + th * 4.0f / 3.0f, r.Max.y - 3.0f);
}

void buildNodeItem(const Project& project, const Node& n, const NodeItemState& view, StackLayout stack, NodeItem& out)
{
    Builder(project, n, view, dynamic_cast<const CompositorNode*>(&n) ? stack : StackLayout{}, out).build();
}
