#pragma once
// The live picture of an edit being dragged in the graph (EditPreview), drawn on the GPU: while a
// slider or offset moves, no tile is recomputed on the CPU.
//
// begin() finds the nodes the edit reaches (the edited node and everything downstream, through
// inputs and masks, up to the master). Whatever else they read is frozen into tile atlases
// ("leaves", rendered as they become visible, at the zoom's level of detail):
//   - in each recomposed stack, the runs of layers the edit doesn't reach,
//   - the raw wet of reached nodes that don't read an input (or a Move/Reference's input),
//   - every mask the edit doesn't reach.
// Each frame the reached nodes are rebuilt from the master down, in view space: leaves drawn
// through a shifted camera (that's how a Move moves), signals applied by preview-signal.wgsl,
// stacks recomposed. A node reached twice is built once per offset it's seen through.
//
// A frame where neither the edit nor the view moved renders nothing; an unchanged leaf pass
// keeps its texture. begin() is false when the edit doesn't reach the master.

#include "core/project.hpp"
#include "canvas/item.hpp"
#include "canvas/tiles/atlas.hpp"
#include "canvas/tiles/pyramid.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <functional>
#include <memory>
#include <vector>

class EditPreviewItem : public CanvasItem {
public:
    explicit EditPreviewItem(Project& project) : m_project(project) {}
    ~EditPreviewItem() override { releaseGpu(); }

    bool   begin();   // plans the project's current preview; false if it can't be shown here
    void   end();     // drops the plan and the frozen tiles
    bool   active() const { return m_active; }
    NodeId node() const { return m_node; }
    bool   hasResult() const { return m_hasResult; }   // a complete picture was rendered

    bool prepare(CanvasFrame& frame) override;
    void drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override;
    bool settled() const override { return !m_active || m_leavesReady; }

private:
    // A frozen input: tiles of something the edit can't change, kept per level of detail.
    struct Leaf {
        TilePyramid pyramid;
        struct Level {
            TileSet      done;               // uploaded (empty results included)
            TileAtlas    atlas;
            TileDrawList draw;               // every uploaded tile
            uint64_t     revision = 0;       // bumps with every upload
            uint64_t     placed   = ~0ull;   // the revision the draw was built for
        } levels[TilePyramid::kLevels];
    };
    // Where a node reads something from: a frozen leaf, or a reached node rebuilt each frame.
    struct Source { int leaf = -1, plan = -1; };
    // A node the edit reaches.
    struct Plan {
        NodePtr node;
        Source  input;                                 // unary nodes
        std::vector<Source> layers;                    // a stack the edit reaches: recomposed, bottom first
        int     wetLeaf = -1;                          // otherwise its raw wet, frozen
        Source  masks[kSignalCount][kChannelCount];
    };
    // The passes of one frame, built from the plan and the current values.
    struct Op {
        enum class Kind : uint8_t { Leaf, Signal, Stack } kind;
        int  leaf = -1;  Vec2 offset;                 // Leaf: drawn shifted by offset (world)
        int  plan = -1;                               // Signal
        int  wet = -1, dry = -1;
        int  masks[kPreviewMaskTextures];
        std::vector<int> layers;                      // Stack: bottom first
    };
    // An op already built this frame: a plan's output (or a leaf) seen through an offset.
    struct Built { int plan, leaf; Vec2 offset; int op; };
    struct Target { WGPUTexture texture = nullptr; WGPUTextureView view = nullptr; };
    // What a leaf pass drew into its target: the same again needs no pass.
    struct LeafPass {
        int      leaf = -1, level = -1;
        uint64_t revision = 0;
        float    offsetX = 0, offsetY = 0, panX = 0, panY = 0, zoom = 0, rotation = 0;
        bool operator==(const LeafPass&) const = default;
    };
    // Everything a frame's result depends on: the same again renders nothing.
    struct FrameKey {
        EditPreview::Kind kind = EditPreview::Kind::None;
        NodeId   node = kNoNodeId;
        int      signal = 0, channel = 0, x = 0, y = 0;
        float    scale = 0, panX = 0, panY = 0, zoom = 0, rotation = 0;
        uint32_t width = 0, height = 0;
        int      level = -1;
        uint64_t revisions = 0;   // of every leaf's current level
        bool operator==(const FrameKey&) const = default;
    };
    // Uniforms of one kind; each pass of a frame takes its own.
    struct UniformPool {
        std::vector<GpuUniform> items;
        size_t used = 0;
    };

    int  leafFor(const NodePtr& n, bool wetOnly);
    int  stackLeaf(std::vector<NodePtr> layers);
    Source sourceFor(const NodePtr& n);
    int  planFor(const NodePtr& n);

    int  build(int plan, Vec2 offset);        // ops for a reached node's output; returns the op index
    int  opFor(Source src, Vec2 offset);
    int  leafOp(int leaf, Vec2 offset);
    int  signalOp(int plan, int wet, int dry, Vec2 offset);
    float scale(const Node& n, NodeSignal s, NodeChannel c) const;

    TileRect  leafRange(const CanvasFrame& frame, const Op& op) const;   // its tiles on screen, at m_level
    FrameKey  frameKey(const CanvasFrame& frame) const;
    bool catchUp(CanvasFrame& frame);         // renders missing visible leaf tiles; true if all there
    void execute(CanvasFrame& frame);
    Target&  target(const CanvasFrame& frame, size_t i);
    GpuUniform& uniform(const CanvasFrame& frame, UniformPool& pool, WGPUBindGroupLayout layout, uint64_t size);
    void releaseTargets();

    Project& m_project;
    bool   m_active = false;
    NodeId m_node   = kNoNodeId;
    std::vector<std::unique_ptr<Leaf>> m_leaves;
    absl::flat_hash_map<const void*, int> m_leafIndex[2];   // node -> leaf (output / raw wet)
    absl::flat_hash_set<const Node*> m_reached;   // the edit and everything downstream of it
    std::vector<Plan> m_plans;
    absl::flat_hash_map<const Node*, int> m_planIndex;
    int  m_masterPlan = -1;

    std::vector<Op>    m_ops;
    std::vector<Built> m_built;
    bool m_leavesReady = false;
    bool m_hasResult   = false;
    RenderCtx m_render;
    int  m_level = 0;                         // the level of detail leaves are drawn at

    // redoing only what changed
    FrameKey m_rendered;       // what the result shows
    std::vector<LeafPass> m_targetContent;   // per target, what a leaf pass left in it

    // GPU, sized to the view
    std::vector<Target>  m_targets;
    uint32_t             m_targetW = 0, m_targetH = 0;
    UniformPool          m_cameras;         // atlas pipeline group 0, per leaf pass
    UniformPool          m_params;          // signal pipeline group 0, per signal pass
    Target               m_dummy;           // 1x1 transparent, for unused texture slots
    WGPUBindGroup        m_resultGroup = nullptr;
};
