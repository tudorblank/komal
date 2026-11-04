// The edit in progress on the CPU (PreviewModel) against what the document becomes once it's
// applied: the GPU preview renders from the model, so a rule the model gets wrong shows wrong
// pictures. On random graphs: every node's bounds as the model says them under the edit, then as
// the document has them after; and every node the edit changed must be one the plan reaches.

#include "document/preview-model.hpp"
#include "helpers.hpp"

#include <random>

namespace {

class Graphs {
public:
    explicit Graphs(uint32_t seed) : m_rng(seed) {}

    int   pick(int n) { return std::uniform_int_distribution<int>(0, n - 1)(m_rng); }
    float unit() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng); }
    NodeId any(const Project& p) { return p.nodes()[size_t(pick(int(p.nodes().size())))]->id(); }

    // The starter project grown at random: rasters with pixels, Moves, stacks, references, masks,
    // muted layers, artboards.
    void grow(Project& p)
    {
        for (int i = 0; i < 3; ++i) {
            const int x0 = pick(300) - 150, y0 = pick(300) - 150, w = 1 + pick(90);
            rasterWith(p, [&](TileGrid& g) {
                for (int y = 0; y < 12; ++y)
                    for (int x = 0; x < w; x += 2) fillPixel(g, x0 + x, y0 + y, RGBA{ 200, 10, 10, 255 });
            });
        }
        for (int i = 0; i < 12; ++i) {
            const NodeType type = NodeType(pick(4));
            step(p, "Add", [&] { p.addNode(type, 0.0f, 0.0f, { .from = unit() < 0.6f ? any(p) : kNoNodeId, .into = kNoNodeId, .slot = {} }); });
        }
        for (int i = 0; i < 25; ++i) {
            const NodeId from = any(p), to = any(p);
            const InputSlot slots[] = { InputSlot::input(), InputSlot::newLayer(uint32_t(pick(3))),
                                        InputSlot::mask(NodeSignal(pick(3)), NodeChannel(pick(4))) };
            const InputSlot slot = slots[pick(3)];
            if (p.canConnect(from, to, slot)) step(p, "Connect", [&] { p.connect(from, to, slot); });
        }
        for (const NodePtr& n : p.nodes())   // some offsets, so Moves move
            if (std::holds_alternative<MoveSettings>(n->settings()))
                step(p, "Offset", [&] { p.setNodeSettings(n->id(), MoveSettings{ pick(200) - 100, pick(200) - 100 }); });
        // Layers muted where they are, and artboards on the master with a fill and layers in them.
        for (const NodePtr& n : p.nodes())
            if (const auto* c = dynamic_cast<const CompositorNode*>(n.get()); c && !c->layers().empty() && unit() < 0.5f) {
                const NodeId layer = c->layers()[size_t(pick(int(c->layers().size())))]->id();
                step(p, "Mute", [&] { p.setLayerMuted(n->id(), layer, true); });
            }
        for (int i = 0; i < 2; ++i) {
            const int x0 = pick(300) - 150, y0 = pick(300) - 150;
            ArtboardId id = kNoArtboard;
            step(p, "Artboard", [&] { id = p.addArtboard({ x0, y0, x0 + 20 + pick(80), y0 + 20 + pick(80) }); });
            Artboard a = *p.artboard(id);
            a.color = Color8{ 10, 200, 10, uint8_t(100 + pick(156)) };
            step(p, "Color", [&] { p.setArtboard(a); });
            const auto& layers = p.master().layers();
            if (layers.empty()) continue;
            const NodeId layer = layers[size_t(pick(int(layers.size())))]->id();
            step(p, "Into", [&] { p.moveStackRows(p.master().id(), { { layer }, {}, id, 0 }); });
        }
    }

    std::mt19937 m_rng;
};

std::vector<std::pair<NodeId, BoundsI>> boundsOf(const Project& p, const PreviewModel* model)
{
    std::vector<std::pair<NodeId, BoundsI>> out;
    for (const NodePtr& n : p.nodes()) out.emplace_back(n->id(), model ? model->bounds(n->id()) : n->bounds());
    return out;
}

}

TEST_CASE("the preview model's bounds are the document's once the edit is applied")
{
    int made[3] = {};   // edits of each kind actually tried
    for (const uint32_t seed : { 11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u }) {
        StarterProject s;
        Project& p = s.project;
        Graphs g(seed);
        g.grow(p);
        for (int round = 0; round < 30; ++round) {
            CAPTURE(seed);
            CAPTURE(round);
            const auto before = boundsOf(p, nullptr);
            const NodeId target = g.any(p);
            const int kind = g.pick(3);
            bool exact = true;   // a stroke that erases may leave less than it could cover
            if (kind == 0) {
                const NodeSignal sig = NodeSignal(g.pick(3));
                if (!p.find(target)->hasSignal(sig)) continue;
                if (!p.previewEdit(ScaleEdit{ target, sig, NodeChannel(g.pick(4)), float(g.pick(5)) / 4.0f })) continue;
            } else if (kind == 1) {
                if (!std::holds_alternative<MoveSettings>(p.find(target)->settings())) continue;
                if (!p.previewEdit(OffsetEdit{ target, g.pick(400) - 200, g.pick(400) - 200 })) continue;
            } else {
                if (p.find(target)->type() != NodeType::Raster) continue;
                p.setActiveRaster(target);
                if (!p.activeRasterEditable()) continue;
                BrushParams brush;
                brush.diameter = 2.0f + g.unit() * 30.0f;
                brush.mode = g.unit() < 0.3f ? CompositeMode::Erase : CompositeMode::Over;
                exact = false;
                p.beginStep("Stroke");
                p.beginStroke(brush, { g.unit() * 300.0f - 150.0f, g.unit() * 300.0f - 150.0f, 1.0f });
                const StrokePoint to[] = { { g.unit() * 300.0f - 150.0f, g.unit() * 300.0f - 150.0f, 1.0f } };
                p.strokeTo(to);
            }
            REQUIRE(p.preview());
            ++made[kind];
            PreviewModel model(p);
            REQUIRE(model.plan(*p.preview()));
            const auto predicted = boundsOf(p, &model);

            if (kind == 2) {
                p.endStroke();
                p.endStep();
            } else {
                step(p, "Apply", [&] { p.applyPreview(); });
            }
            const auto after = boundsOf(p, nullptr);
            REQUIRE(after.size() == predicted.size());
            for (size_t i = 0; i < after.size(); ++i) {
                const auto& [id, actual] = after[i];
                CAPTURE(uint32_t(id));
                const BoundsI expected = predicted[i].second;
                if (exact) CHECK(actual == expected);
                else       CHECK(expected.contains(actual));
                if (actual != before[i].second) CHECK(model.plan().reaches(id));   // it changed: the plan knew
            }
        }
    }
    CHECK(made[0] >= 20);
    CHECK(made[1] >= 10);
    CHECK(made[2] >= 10);
}
