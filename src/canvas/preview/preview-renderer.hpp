#pragma once
// A preview's picture in one space: the passes that rebuild a reached node's output on the GPU
// from its leaves (frozen tiles) and the values being dragged, into textures of its own. One per
// picture: the canvas has one, the navigator one, each previewed thumbnail one.
//
//   if (renderer.plan(preview, node)) {
//       const PreviewSpace space = PreviewSpace::fitted(renderer.bounds(), w, h);
//       if (renderer.render(preview, space, pipelines, encoder, budget)) ... renderer.result() is new
//   }
//
// The passes, per reached node, as Node::fetch does on the CPU:
//   leaf     a leaf's tiles drawn through the space's camera, shifted (that's how a Move moves)
//   stroke   the stroke's coverage over a raster's own pixels (preview-stroke.wgsl)
//   stack    layers composited bottom first: leaves drawn straight in, rebuilt layers blitted
//   signal   a node's signals: Main(Wet over Dry), masks included (preview-signal.wgsl)
// A node reached twice is built once per offset it's seen through. Nothing is rendered until
// every leaf tile the passes read is there (built within the budget, as they become visible); a
// frame whose passes, values, leaves and space are all as last rendered renders nothing, and a
// leaf pass whose texture already shows it is skipped.

#include "canvas/item.hpp"
#include "canvas/preview/preview-plan.hpp"
#include "canvas/tiles/atlas.hpp"
#include "gfx/texture.hpp"

#include <absl/container/flat_hash_map.h>
#include <memory>
#include <optional>
#include <vector>

class Preview;

// Where a picture is drawn: a camera over the world, the picture's size, and the level of detail
// its leaves are read at.
struct PreviewSpace {
    CanvasCamera camera;
    float    viewW = 0, viewH = 0;     // the picture in points (the camera's units)
    uint32_t pixelW = 0, pixelH = 0;   // ... in pixels
    ViewRect visible{};                // the world rectangle it shows
    int      level = 0;

    static PreviewSpace ofView(const CanvasFrame& frame);                                   // a canvas view's
    static PreviewSpace fitted(const BoundsI& bounds, uint32_t width, uint32_t height);   // bounds filling width x height
};

class PreviewRenderer {
public:
    PreviewRenderer() = default;
    ~PreviewRenderer() { release(); }
    PreviewRenderer(const PreviewRenderer&) = delete;
    PreviewRenderer& operator=(const PreviewRenderer&) = delete;

    // This frame's passes for `node`'s output under the edit. False: the edit doesn't reach it.
    bool    plan(const Preview& preview, NodeId node);
    BoundsI bounds() const;   // world: what the planned output can cover (Preview::bounds)
    // Brings the leaf tiles the passes read up to date within the budget; once all are there,
    // records the passes into `enc`, unless the result already shows exactly this. True: a new
    // result was recorded.
    bool render(const Preview& preview, const PreviewSpace& space, const CanvasPipelines& p, WGPUCommandEncoder enc, const Deadline& budget);

    bool ready() const { return m_ready; }          // every leaf tile the last render read was there
    bool hasResult() const { return m_hasResult; }  // a complete picture of this plan
    WGPUTextureView result() const;                 // from (0, 0), the space's pixel size
    void forget();    // the results and draw lists (a new plan)
    void release();   // and the GPU resources

private:
    struct Layer { int op = -1, leaf = -1; Vec2 offset; };   // a stack's: a pass blitted, or a leaf drawn shifted
    struct Op {
        enum class Kind : uint8_t { Leaf, Stroke, Stack, Signal } kind = Kind::Leaf;
        int  leaf = -1;  Vec2 offset;                 // Leaf
        int  pixels = -1, coverage = -1;              // Stroke: the raster's own pixels and the coverage (passes)
        std::vector<Layer> layers;                    // Stack: bottom first
        int  node = -1, wet = -1, dry = -1;           // Signal: whose signals, applied to which passes
        int  masks[kPreviewMaskTextures];
    };
    struct Built { int node, leaf; Vec2 offset; int op; };   // built this frame: a node (or leaf) seen through an offset
    // What a leaf pass drew into its target: the same again needs no pass.
    struct LeafPass {
        int      leaf = -1, level = -1;
        uint64_t revision = 0;
        float    offsetX = 0, offsetY = 0, panX = 0, panY = 0, zoom = 0, rotation = 0;
        uint32_t width = 0, height = 0;
        bool operator==(const LeafPass&) const = default;
    };
    // Everything a result depends on: the same again renders nothing.
    struct FrameKey {
        uint64_t serial = 0;
        int      target = -1;
        std::optional<EditPreview> edit;
        float    panX = 0, panY = 0, zoom = 0, rotation = 0, viewW = 0, viewH = 0;
        uint32_t width = 0, height = 0;
        int      level = -1;
        uint64_t revisions = 0;   // of every leaf read, at the level
        bool operator==(const FrameKey&) const = default;
    };
    struct LeafDraw { TileDrawList draw; uint64_t placed = ~0ull; };   // every tile of a leaf at a level
    struct UniformPool { std::vector<GpuUniform> items; size_t used = 0; };   // each pass of a frame takes its own

    int   build(int node, Vec2 offset);
    int   opFor(PreviewPlan::Source src, Vec2 offset);
    int   leafOp(int leaf, Vec2 offset);
    int   push(Op op);

    bool catchUp(const PreviewSpace& space, const CanvasPipelines& p, const Deadline& budget);
    bool catchUpLeaf(int leaf, Vec2 offset, const PreviewSpace& space, const CanvasPipelines& p, const Deadline& budget);
    FrameKey frameKey(const PreviewSpace& space) const;
    bool execute(const PreviewSpace& space, const CanvasPipelines& p, WGPUCommandEncoder enc);   // false: no GPU memory for it
    void drawLeaf(WGPURenderPassEncoder pass, int leaf, Vec2 offset, const PreviewSpace& space, const CanvasPipelines& p);
    GpuTexture* target(const PreviewSpace& space, const CanvasPipelines& p, size_t i);   // null: couldn't make it
    GpuUniform* uniform(const CanvasPipelines& p, UniformPool& pool, WGPUBindGroupLayout layout, uint64_t size);

    const Preview* m_preview = nullptr;   // between plan() and render()
    uint64_t m_serial = 0;                // the plan the results are of
    int      m_target = -1;               // the planned node (its plan index)
    std::vector<Op>    m_ops;             // this frame's: the target node's output is the last
    std::vector<Built> m_built;
    bool m_ready = false;
    bool m_hasResult = false;
    size_t m_result = 0;                  // the target holding it
    FrameKey m_rendered;
    std::vector<LeafPass> m_content;      // per target, what a leaf pass left in it
    absl::flat_hash_map<int, std::unique_ptr<LeafDraw>> m_draws;   // per leaf * kLevels + level

    // GPU
    std::vector<GpuTexture> m_targets;    // one per op, at least the space's size
    uint32_t    m_targetW = 0, m_targetH = 0;
    UniformPool m_cameras;                // atlas pipeline group 0, per leaf drawn
    UniformPool m_signalParams;
    UniformPool m_strokeParams;
    GpuTexture  m_empty;                  // 1x1 transparent, for unused texture slots
};
