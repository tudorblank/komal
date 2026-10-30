#include "node.hpp"

#include <algorithm>

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

// ============================ NODE ============================
Node::Node()
{
    m_signals[int(NodeSignal::Dry)].scale[int(NodeChannel::A)] = 0.0f;   // dry is off by default
}

const Tile* Node::fetch(int tx, int ty, RenderCtx& ctx)
{
    if (!TileUtils::tileOverlaps(bounds(), tx, ty)) return nullptr;
    if (isPassthrough()) return fetchWet(tx, ty, ctx);   // zero-copy

    // Main first: where its alpha is zero here, nothing below is fetched.
    Gain main[4];
    if (!planGains(signal(NodeSignal::Main), tx, ty, ctx, main)) return nullptr;

    if (!dryOn()) {
        // Main(Wet(raw)), folded into one pass when the masks allow.
        Gain wet[4];
        if (!planGains(signal(NodeSignal::Wet), tx, ty, ctx, wet)) return nullptr;
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

// ---------------------------------------------------------------- signals / masks

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

bool Node::linkMask(GraphKey, NodePtr mask, NodeSignal sig, NodeChannel ch)
{
    if (!hasSignal(sig) || !canDependOn(mask)) return false;
    NodePtr& slot = m_signals[int(sig)].mask[int(ch)];
    if (slot == mask) return true;
    changeKnown(footprint(), Bounds::Keep, Evict::Output, [&] { slot = std::move(mask); });
    return true;
}

bool Node::hasMasks() const
{
    for (const auto& s : m_signals) for (const auto& m : s.mask) if (m) return true;
    return false;
}

bool Node::clearMasksTo(GraphKey, const NodePtr& n)
{
    if (!n || !usesAsMask(n.get())) return false;
    changeKnown(footprint(), Bounds::Keep, Evict::Output, [&] {
        for (auto& s : m_signals) for (auto& m : s.mask) if (m == n) m = nullptr;
    });
    return true;
}

void Node::unlinkAllMasks(GraphKey)
{
    if (!hasMasks()) return;
    changeKnown(footprint(), Bounds::Keep, Evict::Output, [&] {
        for (auto& s : m_signals) for (auto& m : s.mask) m = nullptr;
    });
}

// ---------------------------------------------------------------- graph

std::vector<NodePtr> Node::allDeps() const
{
    std::vector<NodePtr> out;
    auto add = [&](const NodePtr& n) { if (n && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n); };
    for (const auto& in : inputs()) add(in);
    for (const auto& s : m_signals) for (const auto& m : s.mask) add(m);
    return out;
}

bool Node::usesAsInput(const Node* n) const
{
    for (const auto& in : inputs()) if (in.get() == n) return true;
    return false;
}

bool Node::usesAsMask(const Node* n) const
{
    for (const auto& s : m_signals) for (const auto& m : s.mask) if (m.get() == n) return true;
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

    const NodePtr self = shared_from_this();
    for (auto& u : users) {
        if (u->usesAsInput(this)) u->replaceInputInstance(key, self, nullptr);
        u->clearMasksTo(key, self);
    }
    detachAllInputs(key);
    unlinkAllMasks(key);
}

void Node::dependencyDirty(const Node* from, const TileSet& tiles)
{
    const bool asInput = usesAsInput(from);
    TileSet out;
    if (asInput) {
        mapInputTiles(tiles, out);
        if (dryOn()) out.insert(tiles.begin(), tiles.end());   // dry passes the input unmapped
    }
    if (usesAsMask(from)) out.insert(tiles.begin(), tiles.end());
    emitDirty(out, asInput ? Evict::All : Evict::Output);
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

void Node::emitDirty(const TileSet& tiles, Evict e)
{
    ++m_revision;
    if (tiles.empty()) return;

    for (const auto& w : m_watches)
        if (auto watch = w.lock()) watch->add(tiles);
    std::erase_if(m_watches, [](const std::weak_ptr<TileWatch>& w) { return w.expired(); });

    for (TileKey k : tiles) {
        const auto p = TileUtils::unpackKey(k);
        evictOutputTile(p.x, p.y);                    // always
        if (e == Evict::All) evictRawTile(p.x, p.y);  // only when my inputs changed
    }
    for (auto& w : std::vector<std::weak_ptr<Node>>(m_listeners))
        if (auto l = w.lock()) l->dependencyDirty(this, tiles);
    std::erase_if(m_listeners, [](const std::weak_ptr<Node>& w) { return w.expired(); });
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
    for (auto& d : m_wired)
        if (std::find(now.begin(), now.end(), d) == now.end())
            std::erase_if(d->m_listeners, [&](const std::weak_ptr<Node>& w) { auto l = w.lock(); return !l || l.get() == this; });
    for (auto& d : now)
        if (std::find(m_wired.begin(), m_wired.end(), d) == m_wired.end())
            d->m_listeners.push_back(weak_from_this());
    m_wired = std::move(now);
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

bool Node::planGains(const SignalChannels& s, int tx, int ty, RenderCtx& ctx, Gain (&g)[4])
{
    for (int c = 0; c < kChannelCount; ++c) {
        g[c].k = gainByte(s.scale[c]);
        g[c].perPixel = nullptr;
        if (g[c].k != 0 && s.mask[c]) {
            const MaskTile* m = maskTile(*s.mask[c], tx, ty, ctx);
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
    if (!planGains(signal(sig), tx, ty, ctx, g)) return nullptr;   // before fetching
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
