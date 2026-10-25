#pragma once
// The live picture of an edit being dragged in the graph (EditPreview), drawn entirely on the
// GPU: while a slider or an offset moves, no tile is recomputed on the CPU.
//
// When the drag starts, the nodes the edit reaches are found: the edited node and everything
// downstream of it, through inputs and masks alike, up to the master. Everything else they read
// is frozen into tile atlases, once (rendered as it becomes visible, within the frame budget):
//   - in every stack that is recomposed, the runs of layers the edit doesn't reach,
//   - the raw wet of reached nodes that don't take it from an input (the edit itself, a raster
//     reached only through its masks, ...), or for a Move/Reference the output of its input,
//   - every mask the edit doesn't reach.
// Each frame the GPU then rebuilds the reached nodes from the master down, pass by pass in view
// space: frozen leaves drawn through a shifted camera (that is how a Move moves), each node's
// signals applied by preview-signal.wgsl (scales, masks, Wet over Dry, Main), each stack
// recomposed. A node reached twice (used by two nodes, or as an input and a mask) is built once
// per offset it is seen through and shared.
//
// begin() returns false when the edit doesn't reach the master (nothing on screen changes).

#include "core/project.h"
#include "gfx/canvas-scene.h"
#include "gfx/tile-atlas.h"

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
    void draw(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override;
    bool settled() const override { return !m_active || m_leavesReady; }

private:
    // A frozen input: tiles of something the edit can't change, rendered on demand, kept.
    struct Leaf {
        std::function<const Tile*(int tx, int ty, RenderCtx& ctx)> render;
        TileSet   footprint;
        TileSet   done;       // rendered (empty results included)
        TileAtlas atlas;
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
    struct Uniform { WGPUBuffer buffer = nullptr; WGPUBindGroup group = nullptr; };
    // Uniforms of one kind; a frame takes fresh ones (every pass needs its own contents).
    struct UniformPool {
        std::vector<Uniform> items;
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

    bool catchUp(CanvasFrame& frame);         // renders missing visible leaf tiles; true if all there
    void execute(CanvasFrame& frame);
    Target&  target(const CanvasFrame& frame, size_t i);
    Uniform& uniform(const CanvasFrame& frame, UniformPool& pool, WGPUBindGroupLayout layout, uint64_t size);
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

    // GPU, sized to the view
    std::vector<Target>  m_targets;
    uint32_t             m_targetW = 0, m_targetH = 0;
    UniformPool          m_cameras;         // atlas pipeline group 0, per leaf pass
    UniformPool          m_params;          // signal pipeline group 0, per signal pass
    Target               m_dummy;           // 1x1 transparent, for unused texture slots
    WGPUBindGroup        m_resultGroup = nullptr;
};
