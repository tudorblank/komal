// History under random use: edits of every kind, undone, redone and jumped across in any order,
// branches included. Every step reached again must look exactly as it did when it was made: the
// graph (a stack's mutes and groups too), the artboards, each raster's pixels and bounds, and the
// master as rendered (caches included). Each artboard is one of the master's groups throughout.

#include "helpers.hpp"

#include <map>
#include <random>
#include <string>

namespace {

uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * 0x100000001B3ull; }

uint64_t hashTile(uint64_t h, int tx, int ty, const Tile& t)
{
    h = mix(h, TileUtils::packKey(tx, ty));
    for (int i = 0; i < Tile::kPixels; ++i) {
        const RGBA c = t.getReadAll()[i];
        h = mix(h, uint64_t(c.r) | uint64_t(c.g) << 8 | uint64_t(c.b) << 16 | uint64_t(c.a) << 24);
    }
    return h;
}

std::string bounds(const BoundsI& b) { return b.empty() ? "empty" : std::to_string(b.minX) + "," + std::to_string(b.minY) + ".." + std::to_string(b.maxX) + "," + std::to_string(b.maxY); }

// Everything history restores, written out (the active raster and the selection aren't history).
std::string fingerprint(Project& p)
{
    std::string out;
    for (const NodePtr& n : p.nodes()) {
        out += "node " + std::to_string(uint32_t(n->id())) + " type " + std::to_string(int(n->type())) + " at " +
               std::to_string(n->x()) + "," + std::to_string(n->y()) + " bounds " + bounds(n->bounds());
        const NodeSettings settings = n->settings();
        if (const auto* m = std::get_if<MoveSettings>(&settings)) out += " move " + std::to_string(m->x) + "," + std::to_string(m->y);
        if (const auto* r = std::get_if<RasterSettings>(&settings)) out += " locks " + std::to_string(r->locked) + std::to_string(r->alphaLocked);
        for (int s = 0; s < kSignalCount; ++s)
            for (int c = 0; c < kChannelCount; ++c) out += " " + std::to_string(Node::gainByte(n->signal(NodeSignal(s)).scale[c]));
        if (const auto* c = std::get_if<CompositorSettings>(&settings)) {
            for (NodeId id : c->muted) out += " muted " + std::to_string(uint32_t(id));
            for (const StackGroup& g : c->groups)
                out += " group " + std::to_string(uint32_t(g.key)) + ":" + std::to_string(g.begin) + "-" + std::to_string(g.end);
        }
        for (const auto& [source, slot] : n->links().all()) out += " <" + std::to_string(uint32_t(source->id())) + "@" + slot.text();
        if (n->type() == NodeType::Raster) {
            const RasterData& r = static_cast<const RasterRootNode&>(*n).raster();
            uint64_t h = 0;   // a sum: the tiles in any order
            r.forEachTile([&](int tx, int ty, const Tile& t) { h += hashTile(1469598103934665603ull, tx, ty, t); });
            out += " pixels " + std::to_string(h) + " raster " + bounds(r.getBounds());
        }
        out += "\n";
    }
    uint64_t master = 0;
    RenderCtx ctx;
    for (const TileKey key : p.master().footprint()) {
        const auto [tx, ty] = TileUtils::unpackKey(key);
        if (const Tile* t = renderTile(p.master(), tx, ty, ctx)) master += hashTile(1469598103934665603ull, tx, ty, *t);
    }
    for (const Artboard& a : p.artboards())
        out += "artboard " + std::to_string(uint32_t(a.id)) + " " + a.name + " " + bounds(a.rect) + " " + std::to_string(a.color.r) + "," +
               std::to_string(a.color.g) + "," + std::to_string(a.color.b) + "," + std::to_string(a.color.a) + (a.visible ? "" : " hidden") + "\n";
    out += "master " + std::to_string(master) + " " + bounds(p.master().bounds()) + "\n";
    return out;
}

class Fuzzer {
public:
    Fuzzer(Project& p, uint32_t seed) : m_p(p), m_rng(seed) {}

    int pick(int n) { return std::uniform_int_distribution<int>(0, n - 1)(m_rng); }
    float unit() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng); }
    NodeId anyNode() { return m_p.nodes()[size_t(pick(int(m_p.nodes().size())))]->id(); }

    // One random edit, as one step (it may change nothing: then there's no step).
    void edit()
    {
        Project& p = m_p;
        switch (pick(14)) {
        case 0: {
            const NodeType type = NodeType(pick(4));
            step(p, "Add", [&] { p.addNode(type, unit() * 500.0f, unit() * 500.0f, { .from = unit() < 0.5f ? anyNode() : kNoNodeId, .into = kNoNodeId, .slot = {} }); });
            break;
        }
        case 1: {   // a link, wherever it may go
            const NodeId from = anyNode(), to = anyNode();
            const InputSlot slots[] = { InputSlot::input(), InputSlot::newLayer(uint32_t(pick(3))), InputSlot::stackLayer(uint32_t(pick(3))),
                                        InputSlot::mask(NodeSignal(pick(3)), NodeChannel(pick(4))) };
            const InputSlot slot = slots[pick(4)];
            if (p.canConnect(from, to, slot)) step(p, "Connect", [&] { p.connect(from, to, slot); });
            break;
        }
        case 2: {   // a link off
            const NodeId to = anyNode();
            const auto into = p.find(to)->links().all();
            if (!into.empty()) step(p, "Disconnect", [&] { p.disconnect(to, into[size_t(pick(int(into.size())))].second); });
            break;
        }
        case 3:
            if (p.nodes().size() > 3) step(p, "Delete", [&] { p.deleteNodes({ anyNode() }); });
            break;
        case 4: {
            const NodeId id = anyNode();
            if (std::holds_alternative<MoveSettings>(p.find(id)->settings()))
                step(p, "Offset", [&] { p.setNodeSettings(id, MoveSettings{ pick(301) - 150, pick(301) - 150 }); });
            break;
        }
        case 5: {
            const NodeId id = anyNode();
            const NodeSignal s = NodeSignal(pick(3));
            step(p, "Scale", [&] { p.setSignalScale(id, s, NodeChannel(pick(4)), float(pick(5)) / 4.0f); });
            break;
        }
        case 6: {   // a stroke on some raster
            std::vector<NodeId> rasters;
            for (const NodePtr& n : p.nodes()) if (n->type() == NodeType::Raster) rasters.push_back(n->id());
            if (rasters.empty()) break;
            p.setActiveRaster(rasters[size_t(pick(int(rasters.size())))]);
            BrushParams brush;
            brush.diameter = 2.0f + unit() * 40.0f;
            brush.color = { uint8_t(pick(256)), uint8_t(pick(256)), uint8_t(pick(256)), uint8_t(1 + pick(255)) };
            brush.mode = unit() < 0.25f ? CompositeMode::Erase : CompositeMode::Over;
            step(p, "Stroke", [&] {
                p.beginStroke(brush, { unit() * 300.0f - 150.0f, unit() * 300.0f - 150.0f, 1.0f });
                const StrokePoint to[] = { { unit() * 300.0f - 150.0f, unit() * 300.0f - 150.0f, unit() } };
                p.strokeTo(to);
                p.endStroke();
            });
            break;
        }
        case 7:
            if (p.master().layers().size() >= 2)
                step(p, "Move Layer", [&] { p.moveLayer(p.master().id(), size_t(pick(int(p.master().layers().size()))), size_t(pick(int(p.master().layers().size())))); });
            break;
        case 8: {   // a link lifted and dropped somewhere
            const NodeId from = anyNode();
            const auto into = p.find(from)->links().all();
            if (into.empty()) break;
            const auto& [source, slot] = into[size_t(pick(int(into.size())))];
            const NodeId to = unit() < 0.2f ? kNoNodeId : anyNode();
            const InputSlot toSlot = unit() < 0.5f ? InputSlot::input() : InputSlot::newLayer(uint32_t(pick(3)));
            const NodeId src = source->id();
            const InputSlot fromSlot = slot;
            if (p.canMoveLink(src, from, fromSlot, to, toSlot)) step(p, "Move Link", [&] { p.moveLink(src, from, fromSlot, to, toSlot); });
            break;
        }
        case 10: {   // a layer muted, or shown again, in its compositor
            const NodeId id = anyNode();
            const auto* c = dynamic_cast<const CompositorNode*>(p.find(id));
            if (!c || c->layers().empty()) break;
            const NodeId layer = c->layers()[size_t(pick(int(c->layers().size())))]->id();
            const bool muted = !c->muted(*p.find(layer));
            step(p, "Mute", [&] { p.setLayerMuted(id, layer, muted); });
            break;
        }
        case 11:   // an artboard
            if (p.artboards().size() < 4) {
                const int x0 = pick(200) - 100, y0 = pick(200) - 100;
                step(p, "Artboard", [&] { p.addArtboard({ x0, y0, x0 + 10 + pick(60), y0 + 10 + pick(60) }); });
            }
            break;
        case 12: {   // one changed, or deleted
            if (p.artboards().empty()) break;
            Artboard a = p.artboards()[size_t(pick(int(p.artboards().size())))];
            if (unit() < 0.3f) {
                step(p, "Delete Artboard", [&] { p.deleteArtboard(a.id); });
                break;
            }
            a.color   = Color8{ uint8_t(pick(256)), uint8_t(pick(256)), 40, uint8_t(pick(256)) };
            a.visible = unit() < 0.8f;
            a.rect    = { a.rect.minX + pick(21) - 10, a.rect.minY, a.rect.maxX + pick(21), a.rect.maxY };
            step(p, "Change Artboard", [&] { p.setArtboard(a); });
            break;
        }
        case 13: {   // the master's rows moved together: some layers, maybe an artboard, to a place
            const auto& layers = p.master().layers();
            StackMove m;
            for (const NodePtr& l : layers) if (unit() < 0.3f) m.layers.push_back(l->id());
            const Artboards& boards = p.artboards();
            if (!boards.empty() && unit() < 0.4f) m.groups.push_back(boards[size_t(pick(int(boards.size())))].id);
            else if (!boards.empty() && unit() < 0.6f) m.into = boards[size_t(pick(int(boards.size())))].id;
            m.at = size_t(pick(int(layers.size()) + 2));
            step(p, "Move Rows", [&] { p.moveStackRows(p.master().id(), m); });
            break;
        }
        case 9: {   // an image
            const int x0 = pick(200) - 100, y0 = pick(200) - 100;
            const uint8_t shade = uint8_t(pick(256));
            rasterWith(p, [&](TileGrid& g) {
                for (int y = 0; y < 20; ++y)
                    for (int x = 0; x < 20; x += 3) fillPixel(g, x0 + x, y0 + y, RGBA{ shade, 0, 0, 255 });
            });
            break;
        }
        }
    }

    Project& m_p;
    std::mt19937 m_rng;
};

}

TEST_CASE("history under random use: every step comes back exactly as it was")
{
    for (const uint32_t seed : { 1u, 2u, 3u, 4u, 5u, 6u }) {
        CAPTURE(seed);
        StarterProject s;
        Project& p = s.project;
        Fuzzer f(p, seed);
        std::map<StepId, std::string> seen;   // each step, as it was when made
        seen[p.history().current()] = fingerprint(p);
        for (int i = 0; i < 160; ++i) {
            if (i < 40 || f.unit() < 0.5f) {   // an edit: a new step, maybe a new branch
                const StepId before = p.history().current();
                f.edit();
                REQUIRE(p.artboardsMatchStack());
                if (p.history().current() != before) seen[p.history().current()] = fingerprint(p);
                else REQUIRE(fingerprint(p) == seen[before]);   // no step: nothing changed
                continue;
            }
            const int how = f.pick(3);
            if (how == 0) p.undo();
            else if (how == 1) p.redo();
            else p.goToHistory(StepId(f.pick(int(p.history().size()))));
            const StepId at = p.history().current();
            REQUIRE(p.artboardsMatchStack());
            REQUIRE(seen.contains(at));
            CHECK(fingerprint(p) == seen[at]);
        }
    }
}
