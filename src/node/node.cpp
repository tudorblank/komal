#include "node.hpp"

#include <algorithm>
#include <iterator>

namespace {

int gainAt(const Gain& g, int i) { return g.perPixel ? mul255(g.k, g.perPixel[i]) : g.k; }

// dst (cleared) = src * gains. Canonical in, canonical out. True if non-empty.
bool applyGainInto(Tile& dst, const Tile& src, const Gain (&g)[4])
{
    const RGBA* s = src.getReadAll();
    uint8_t acc = 0;
    {
        Tile::BulkWrite w(dst, Tile::BulkWrite::Trusted);
        RGBA* d = w.data();
        if (!g[0].perPixel && !g[1].perPixel && !g[2].perPixel && !g[3].perPixel) {   // uniform
            const int fA = g[0].k;
            const int fR = mul255(fA, g[1].k), fG = mul255(fA, g[2].k), fB = mul255(fA, g[3].k);
            for (int i = 0; i < Tile::kPixels; ++i) {
                d[i] = { mul255(s[i].r, fR), mul255(s[i].g, fG), mul255(s[i].b, fB), mul255(s[i].a, fA) };
                acc |= d[i].a;
            }
        } else {
            for (int i = 0; i < Tile::kPixels; ++i) {
                if (s[i].a == 0) continue;
                const int kA = gainAt(g[0], i);
                d[i] = { mul255(s[i].r, mul255(kA, gainAt(g[1], i))),
                         mul255(s[i].g, mul255(kA, gainAt(g[2], i))),
                         mul255(s[i].b, mul255(kA, gainAt(g[3], i))),
                         mul255(s[i].a, kA) };
                acc |= d[i].a;
            }
        }
        if (acc) w.markChanged();
    }
    return acc != 0;
}

bool allUnit(const Gain (&g)[4]) { return g[0].isUnit() && g[1].isUnit() && g[2].isUnit() && g[3].isUnit(); }

// a then b as one gain per channel; false if a channel has two per-pixel masks.
bool foldGains(const Gain (&a)[4], const Gain (&b)[4], Gain (&out)[4])
{
    for (int c = 0; c < 4; ++c) {
        if (a[c].perPixel && b[c].perPixel) return false;
        out[c].k        = mul255(a[c].k, b[c].k);
        out[c].perPixel = a[c].perPixel ? a[c].perPixel : b[c].perPixel;
    }
    return true;
}

MaskTile g_fullMask;   // only its address is used: "255 everywhere"

}

// ============================ NAMES ============================
namespace {

constexpr const char* kSignalIds[]     = { "main", "dry", "wet" };   // NodeSignal order
constexpr const char* kSignalLabels[]  = { "Main", "Dry", "Wet" };
constexpr const char* kChannelIds[]    = { "a", "r", "g", "b" };     // NodeChannel order
constexpr const char* kChannelLabels[] = { "A", "R", "G", "B" };
static_assert(std::size(kSignalIds) == kSignalCount && std::size(kSignalLabels) == kSignalCount);
static_assert(std::size(kChannelIds) == kChannelCount && std::size(kChannelLabels) == kChannelCount);

}

const char* signalId(NodeSignal s)      { return kSignalIds[size_t(s)]; }
const char* signalLabel(NodeSignal s)   { return kSignalLabels[size_t(s)]; }
const char* channelId(NodeChannel c)    { return kChannelIds[size_t(c)]; }
const char* channelLabel(NodeChannel c) { return kChannelLabels[size_t(c)]; }

// ============================ RENDER CONTEXT ============================
const MaskTile* const RenderCtx::kFullMask = &g_fullMask;

Tile& RenderCtx::take()
{
    if (m_used == m_tiles.size()) m_tiles.push_back(std::make_unique<Tile>());
    Tile& t = *m_tiles[m_used++];
    t.clear();
    return t;
}

MaskTile& RenderCtx::newMask()
{
    if (m_maskUsed == m_maskPool.size()) m_maskPool.push_back(std::make_unique<MaskTile>());
    return *m_maskPool[m_maskUsed++];
}

bool RenderCtx::findMask(const Node* n, int tx, int ty, const MaskTile*& out) const
{
    auto it = m_masks.find(MaskKey{ n, TileUtils::packKey(tx, ty) });
    if (it == m_masks.end()) return false;
    out = it->second;
    return true;
}

// ============================ LINKS ============================
std::vector<std::pair<NodePtr, InputSlot>> NodeLinks::all() const
{
    std::vector<std::pair<NodePtr, InputSlot>> out;
    if (input) out.push_back({ input, InputSlot::input() });
    for (uint32_t i = 0; i < uint32_t(layers.size()); ++i) out.push_back({ layers[i], InputSlot::stackLayer(i) });
    for (int s = 0; s < kSignalCount; ++s)
        for (int ch = 0; ch < kChannelCount; ++ch)
            if (masks[s][ch]) out.push_back({ masks[s][ch], InputSlot::mask(NodeSignal(s), NodeChannel(ch)) });
    return out;
}

bool NodeLinks::empty() const
{
    if (input || !layers.empty()) return false;
    for (const auto& s : masks) for (const NodePtr& m : s) if (m) return false;
    return true;
}

// ============================ NODE ============================
Node::Node(Ports ports) : m_ports(ports)
{
    m_signals[int(NodeSignal::Dry)].scale[int(NodeChannel::A)] = 0.0f;   // dry is off by default
}

const Tile* Node::fetch(int tx, int ty, RenderCtx& ctx)
{
    if (!TileUtils::tileOverlaps(bounds(), tx, ty)) return nullptr;
    if (isPassthrough()) return fetchWet(tx, ty, ctx);   // zero-copy

    // Main first: where its alpha is zero here, nothing below is fetched.
    Gain main[4];
    if (!planGains(NodeSignal::Main, tx, ty, ctx, main)) return nullptr;

    if (!dryOn()) {
        // Main(Wet(raw)), folded into one pass when the masks allow.
        Gain wet[4];
        if (!planGains(NodeSignal::Wet, tx, ty, ctx, wet)) return nullptr;
        Gain folded[4];
        if (foldGains(wet, main, folded)) return applyGains(fetchWet(tx, ty, ctx), folded, ctx);
        return applyGains(applyGains(fetchWet(tx, ty, ctx), wet, ctx), main, ctx);
    }

    const Tile* wet = wetOn() ? renderSignal(NodeSignal::Wet, tx, ty, ctx) : nullptr;
    const Tile* dry = nullptr;
    if (!wet || !wet->isFullyOpaque())   // dry only shows where wet isn't opaque
        dry = renderSignal(NodeSignal::Dry, tx, ty, ctx);
    return applyGains(blendOver(wet, dry, ctx), main, ctx);
}

BoundsI Node::bounds() const
{
    if (m_boundsDirty) {
        BoundsI b;
        if (wetOn()) b = wetBounds();
        if (dryOn()) b.expand(dryBounds());
        m_bounds = b;
        m_boundsDirty = false;
    }
    return m_bounds;
}

// ---------------------------------------------------------------- signals

bool Node::signalIsIdentity(NodeSignal s) const
{
    for (int c = 0; c < kChannelCount; ++c)
        if (m_signals[int(s)].scale[c] != 1.0f || m_links.masks[int(s)][c]) return false;
    return true;
}

void Node::setScale(GraphKey, NodeSignal sig, NodeChannel ch, float v)
{
    if (!hasSignal(sig)) return;
    v = std::clamp(v, 0.0f, 1.0f);
    float& slot = m_signals[int(sig)].scale[int(ch)];
    if (slot == v) return;

    // Turning Wet or Dry on/off changes bounds and footprint; anything else reshapes the same tiles.
    const bool flips = ch == NodeChannel::A && (gainByte(slot) != 0) != (gainByte(v) != 0);
    if (flips && sig != NodeSignal::Main) change([&] { slot = v; }, Evict::Output);
    else changeKnown(footprint(), Bounds::Keep, Evict::Output, [&] { slot = v; });
}

// ---------------------------------------------------------------- links

std::span<const NodePtr> Node::inputs() const
{
    switch (m_ports.inputs) {
    case InputShape::One:   return { &m_links.input, m_links.input ? 1u : 0u };
    case InputShape::Stack: return m_links.layers;
    case InputShape::None:  break;
    }
    return {};
}

NodePtr Node::linkAt(const InputSlot& slot) const
{
    if (slot.is<InputSlot::Input>()) return m_links.input;
    if (const auto* layer = slot.as<InputSlot::Layer>()) return layer->index < m_links.layers.size() ? m_links.layers[layer->index] : nullptr;
    if (const auto* m = slot.as<InputSlot::Mask>()) return mask(m->signal, m->channel);
    return nullptr;   // a new layer is a position, not a link
}

bool Node::canLink(const NodePtr& source, const InputSlot& slot) const
{
    if (!source || !canDependOn(source)) return false;   // also rules out cycles
    const auto& layers = m_links.layers;
    const bool inStack = std::find(layers.begin(), layers.end(), source) != layers.end();
    if (slot.is<InputSlot::Input>()) return m_ports.inputs == InputShape::One;
    if (const auto* layer = slot.as<InputSlot::Layer>())
        return m_ports.inputs == InputShape::Stack && layer->index < layers.size() && (!inStack || layers[layer->index] == source);
    if (const auto* at = slot.as<InputSlot::NewLayer>()) return m_ports.inputs == InputShape::Stack && at->at <= layers.size();
    if (const auto* m = slot.as<InputSlot::Mask>()) return hasSignal(m->signal);
    return false;
}

// Each port's rule for what changes: an input moves the whole output (bounds too); a layer the
// tiles it covers; a mask reshapes the output where it already is.
void Node::link(GraphKey key, const NodePtr& source, const InputSlot& slot)
{
    assert(canLink(source, slot));
    if (slot.is<InputSlot::Input>()) {
        if (m_links.input != source) change([&] { m_links.input = source; });
    } else if (const auto* layer = slot.as<InputSlot::Layer>()) {
        NodePtr& at = m_links.layers[layer->index];
        if (at == source) return;
        TileSet dirty;
        at->addFootprint(dirty);
        source->addFootprint(dirty);
        changeKnown(std::move(dirty), Bounds::Recompute, Evict::All, [&] { at = source; });
    } else if (const auto* pos = slot.as<InputSlot::NewLayer>()) {
        auto& layers = m_links.layers;
        if (const auto it = std::find(layers.begin(), layers.end(), source); it != layers.end()) {   // in it already: it moves
            const size_t from = size_t(it - layers.begin());
            const size_t to = pos->at > from ? pos->at - 1 : pos->at;   // it leaves its own place first
            if (to != from) moveLayer(key, from, to);
            return;
        }
        TileSet dirty;
        source->addFootprint(dirty);
        changeKnown(std::move(dirty), Bounds::Recompute, Evict::All,
                    [&] { layers.insert(layers.begin() + std::ptrdiff_t(std::min<size_t>(pos->at, layers.size())), source); });
    } else if (const auto* m = slot.as<InputSlot::Mask>()) {
        NodePtr& held = m_links.masks[int(m->signal)][int(m->channel)];
        if (held != source) changeKnown(footprint(), Bounds::Keep, Evict::Output, [&] { held = source; });
    }
}

void Node::unlink(GraphKey, const InputSlot& slot)
{
    if (slot.is<InputSlot::Input>()) {
        if (m_links.input) change([&] { m_links.input = nullptr; });
    } else if (const auto* layer = slot.as<InputSlot::Layer>()) {
        auto& layers = m_links.layers;
        if (layer->index >= layers.size()) return;
        TileSet dirty;
        layers[layer->index]->addFootprint(dirty);
        changeKnown(std::move(dirty), Bounds::Recompute, Evict::All, [&] { layers.erase(layers.begin() + std::ptrdiff_t(layer->index)); });
    } else if (const auto* m = slot.as<InputSlot::Mask>()) {
        NodePtr& held = m_links.masks[int(m->signal)][int(m->channel)];
        if (held) changeKnown(footprint(), Bounds::Keep, Evict::Output, [&] { held = nullptr; });
    }   // a new layer is a position: nothing feeds it
}

void Node::moveLayer(GraphKey, size_t from, size_t to)
{
    auto& layers = m_links.layers;
    if (from >= layers.size() || to >= layers.size() || from == to) return;
    // Only tiles where the moved layer overlaps the layers it crosses can change.
    TileSet moved, others;
    layers[from]->addFootprint(moved);
    for (size_t i = std::min(from, to); i <= std::max(from, to); ++i)
        if (i != from) layers[i]->addFootprint(others);
    TileSet dirty;
    for (TileKey k : moved) if (others.contains(k)) dirty.insert(k);
    changeKnown(std::move(dirty), Bounds::Keep, Evict::All, [&] {
        NodePtr n = std::move(layers[from]);
        layers.erase(layers.begin() + std::ptrdiff_t(from));
        layers.insert(layers.begin() + std::ptrdiff_t(to), std::move(n));
    });
}

void Node::unlinkSource(GraphKey key, const Node& source)
{
    for (const auto& [from, slot] : m_links.all())   // a copy: each unlink edits the links
        if (from.get() == &source) {
            // A layer's row moves down as the ones under it go: find it again.
            if (slot.is<InputSlot::Layer>()) {
                const auto& layers = m_links.layers;
                const auto it = std::find_if(layers.begin(), layers.end(), [&](const NodePtr& l) { return l.get() == &source; });
                if (it != layers.end()) unlink(key, InputSlot::stackLayer(uint32_t(it - layers.begin())));
            } else {
                unlink(key, slot);
            }
        }
}

void Node::setLinks(GraphKey key, const NodeLinks& want)
{
    if (m_links == want) return;
    if (!m_links.empty()) change([&] { m_links = NodeLinks{}; });   // all it held goes first
    if (want.input && canLink(want.input, InputSlot::input())) link(key, want.input, InputSlot::input());
    for (const NodePtr& l : want.layers) {
        const InputSlot top = InputSlot::newLayer(uint32_t(m_links.layers.size()));
        if (l && canLink(l, top)) link(key, l, top);
    }
    for (int s = 0; s < kSignalCount; ++s)
        for (int ch = 0; ch < kChannelCount; ++ch) {
            const InputSlot slot = InputSlot::mask(NodeSignal(s), NodeChannel(ch));
            if (const NodePtr& m = want.masks[s][ch]; m && canLink(m, slot)) link(key, m, slot);
        }
}

// ---------------------------------------------------------------- graph

std::vector<NodePtr> Node::allDeps() const
{
    std::vector<NodePtr> out;
    auto add = [&](const NodePtr& n) { if (n && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n); };
    for (const auto& in : inputs()) add(in);
    for (const auto& s : m_links.masks) for (const auto& m : s) add(m);
    return out;
}

bool Node::usesAsInput(const Node* n) const
{
    for (const auto& in : inputs()) if (in.get() == n) return true;
    return false;
}

bool Node::usesAsMask(const Node* n) const
{
    for (const auto& s : m_links.masks) for (const auto& m : s) if (m.get() == n) return true;
    return false;
}

bool Node::dependsOn(const Node* t) const
{
    absl::flat_hash_set<const Node*> seen;
    return dependsOnImpl(t, seen);
}

bool Node::dependsOnImpl(const Node* t, absl::flat_hash_set<const Node*>& seen) const
{
    for (const auto& d : allDeps()) {
        if (d.get() == t) return true;
        if (seen.insert(d.get()).second && d->dependsOnImpl(t, seen)) return true;
    }
    return false;
}

void Node::removeFromGraph(GraphKey key)
{
    std::vector<NodePtr> users;   // snapshot: the loop below edits m_listeners
    for (auto& w : m_listeners) if (auto l = w.lock()) users.push_back(std::move(l));
    for (auto& u : users) u->unlinkSource(key, *this);
    setLinks(key, NodeLinks{});
}

void Node::receive(const Node& from, const TileSet& tiles, Incoming& in) const
{
    if (usesAsInput(&from)) {
        mapInputTiles(tiles, in.tiles);
        if (dryOn()) in.tiles.insert(tiles.begin(), tiles.end());   // dry passes the input unmapped
        in.raw = true;
    }
    if (usesAsMask(&from)) in.tiles.insert(tiles.begin(), tiles.end());
}

// ---------------------------------------------------------------- change propagation

TileSet Node::change(const std::function<void()>& mutate, Evict e)
{
    TileSet d = footprint();
    mutate();
    TileSet after = footprint();
    d.insert(after.begin(), after.end());
    finishChange(d, Bounds::Recompute, e);
    return d;
}

TileSet Node::changeKnown(TileSet dirty, Bounds b, Evict e, const std::function<void()>& mutate)
{
    mutate();
    finishChange(dirty, b, e);
    return dirty;
}

void Node::finishChange(const TileSet& d, Bounds b, Evict e)
{
    rewire();
    if (b == Bounds::Recompute) invalidateBounds();
    emitDirty(d, e);
}

std::shared_ptr<TileWatch> Node::watchOutput()
{
    auto watch = std::make_shared<TileWatch>();
    m_watches.push_back(watch);
    return watch;
}

void Node::takeDirty(const TileSet& tiles, Evict e)
{
    for (const auto& w : m_watches)
        if (auto watch = w.lock()) watch->add(tiles);
    std::erase_if(m_watches, [](const std::weak_ptr<TileWatch>& w) { return w.expired(); });
    for (TileKey k : tiles) {
        const auto p = TileUtils::unpackKey(k);
        evictOutputTile(p.x, p.y);                    // always
        if (e == Evict::All) evictRawTile(p.x, p.y);  // only when what it reads changed
    }
}

const Node::Downstream& Node::downstreamOrder()
{
    if (m_downstream) return *m_downstream;
    m_downstream = std::make_unique<Downstream>();
    Downstream& d = *m_downstream;

    // Everything reachable through listeners, and how many of the reached (or this) each one reads.
    absl::flat_hash_map<const Node*, int> waiting;
    std::vector<Node*> todo{ this };
    absl::flat_hash_set<const Node*> seen{ this };
    while (!todo.empty()) {
        Node* n = todo.back();
        todo.pop_back();
        for (const auto& w : n->m_listeners)
            if (const NodePtr l = w.lock()) {
                ++waiting[l.get()];
                if (seen.insert(l.get()).second) todo.push_back(l.get());
            }
    }
    // Kahn's algorithm: a node goes once everything it reads that's reached has gone.
    std::vector<Node*> ready{ this };
    for (size_t i = 0; i < ready.size(); ++i)
        for (const auto& w : ready[i]->m_listeners)
            if (const NodePtr l = w.lock(); l && --waiting[l.get()] == 0) {
                d.index.emplace(l.get(), uint32_t(d.order.size()));
                d.order.push_back(l);
                ready.push_back(l.get());
            }
    assert(d.order.size() + 1 == seen.size() && "the graph is acyclic");
    return d;
}

std::vector<NodePtr> Node::downstream()
{
    std::vector<NodePtr> out;
    for (const auto& w : downstreamOrder().order)
        if (NodePtr n = w.lock()) out.push_back(std::move(n));
    return out;
}

void Node::dropOrders()
{
    // Everything upstream has this node's downstream in its own.
    std::vector<Node*> todo{ this };
    absl::flat_hash_set<const Node*> seen{ this };
    while (!todo.empty()) {
        Node* n = todo.back();
        todo.pop_back();
        n->m_downstream.reset();
        for (const NodePtr& d : n->allDeps())
            if (seen.insert(d.get()).second) todo.push_back(d.get());
    }
}

void Node::emitDirty(const TileSet& tiles, Evict e)
{
    if (tiles.empty()) return;
    takeDirty(tiles, e);
    std::erase_if(m_listeners, [](const std::weak_ptr<Node>& w) { return w.expired(); });   // nodes gone: nothing to tell
    if (m_listeners.empty()) return;

    // Downstream, each node once: it takes what everything it reads sent it, then passes it on.
    const Downstream& d = downstreamOrder();
    std::vector<NodePtr> nodes;   // held for the walk
    nodes.reserve(d.order.size());
    for (const auto& w : d.order) nodes.push_back(w.lock());
    std::vector<Incoming> incoming(nodes.size());
    const auto send = [&](const Node& from, const TileSet& out) {
        for (const auto& w : from.m_listeners)
            if (const NodePtr l = w.lock())
                if (const auto it = d.index.find(l.get()); it != d.index.end()) l->receive(from, out, incoming[it->second]);
    };
    send(*this, tiles);
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (!nodes[i] || incoming[i].tiles.empty()) continue;
        Node& n = *nodes[i];
        const TileSet in = std::move(incoming[i].tiles);
        n.takeDirty(in, incoming[i].raw ? Evict::All : Evict::Output);
        send(n, in);
    }
}

void Node::invalidateBounds()
{
    // Already dirty: so are its listeners (they can only recompute through this node).
    if (m_boundsDirty) return;
    m_boundsDirty = true;
    for (auto& w : std::vector<std::weak_ptr<Node>>(m_listeners))
        if (auto l = w.lock(); l && l->usesAsInput(this)) l->invalidateBounds();   // masks don't move bounds
}

void Node::rewire()
{
    assert(!dependsOn(this));
    std::vector<NodePtr> now = allDeps();
    std::vector<NodePtr> relisted;   // deps whose listeners changed: their orders, and those upstream, are stale
    for (auto& d : m_wired)
        if (std::find(now.begin(), now.end(), d) == now.end()) {
            std::erase_if(d->m_listeners, [&](const std::weak_ptr<Node>& w) { auto l = w.lock(); return !l || l.get() == this; });
            relisted.push_back(d);
        }
    for (auto& d : now)
        if (std::find(m_wired.begin(), m_wired.end(), d) == m_wired.end()) {
            d->m_listeners.push_back(weak_from_this());
            relisted.push_back(d);
        }
    m_wired = std::move(now);
    for (const NodePtr& d : relisted) d->dropOrders();
}

// ---------------------------------------------------------------- signal pipeline

const MaskTile* Node::maskTile(Node& mask, int tx, int ty, RenderCtx& ctx)
{
    const MaskTile* m = nullptr;
    if (ctx.findMask(&mask, tx, ty, m)) return m;

    // The mask over white: premultiplied, 255 - a + luma(rgb), with luma(rgb) <= a.
    m = RenderCtx::kFullMask;   // no tile: nothing of the mask here, the channel is untouched
    const size_t mk = ctx.mark();
    if (const Tile* src = mask.fetch(tx, ty, ctx)) {
        MaskTile& out = ctx.newMask();
        const RGBA* p = src->getReadAll();
        uint8_t any = 0, all = 255;
        for (int i = 0; i < Tile::kPixels; ++i) {
            const int luma = (54 * p[i].r + 183 * p[i].g + 19 * p[i].b + 128) >> 8;   // weights sum to 256
            out.v[i] = uint8_t(255 - p[i].a + std::min<int>(luma, p[i].a));
            any |= out.v[i];
            all &= out.v[i];
        }
        m = all == 255 ? RenderCtx::kFullMask : any ? &out : nullptr;
    }
    ctx.rewind(mk);   // the mask node's scratch tiles are no longer needed
    ctx.storeMask(&mask, tx, ty, m);
    return m;
}

bool Node::planGains(NodeSignal sig, int tx, int ty, RenderCtx& ctx, Gain (&g)[4])
{
    const SignalChannels& s = signal(sig);
    for (int c = 0; c < kChannelCount; ++c) {
        g[c].k = gainByte(s.scale[c]);
        g[c].perPixel = nullptr;
        const NodePtr& maskNode = mask(sig, NodeChannel(c));
        if (g[c].k != 0 && maskNode) {
            const MaskTile* m = maskTile(*maskNode, tx, ty, ctx);
            if (!m) g[c].k = 0;                                // mask is 0 here
            else if (m != RenderCtx::kFullMask) g[c].perPixel = m->v;
        }
        if (c == 0 && g[0].k == 0) return false;   // alpha 0: the signal is absent on this tile
    }
    return true;
}

const Tile* Node::applyGains(const Tile* raw, const Gain (&g)[4], RenderCtx& ctx)
{
    if (!raw) return nullptr;
    if (allUnit(g)) return raw;   // zero-copy
    Tile& dst = ctx.take();
    return applyGainInto(dst, *raw, g) ? &dst : nullptr;
}

const Tile* Node::renderSignal(NodeSignal sig, int tx, int ty, RenderCtx& ctx)
{
    Gain g[4];
    if (!planGains(sig, tx, ty, ctx, g)) return nullptr;   // before fetching
    const Tile* raw = sig == NodeSignal::Wet ? fetchWet(tx, ty, ctx) : fetchDry(tx, ty, ctx);
    return applyGains(raw, g, ctx);
}

const Tile* Node::blendOver(const Tile* top, const Tile* bottom, RenderCtx& ctx)
{
    if (!top) return bottom;
    if (!bottom) return top;
    Tile& out = ctx.take();
    {
        Tile::BulkWrite w(out, Tile::BulkWrite::Trusted);
        RGBA* o = w.data();
        const RGBA* t = top->getReadAll();
        const RGBA* b = bottom->getReadAll();
        for (int i = 0; i < Tile::kPixels; ++i)
            o[i] = t[i].a == 255 ? t[i] : t[i].a == 0 ? b[i] : over(b[i], t[i]);
        w.markChanged();
    }
    return &out;
}
