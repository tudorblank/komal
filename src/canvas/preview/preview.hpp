#pragma once
// The edit in progress (Project::preview()) as the GPU shows it: one for the whole app, in the
// canvas engine. It plans the edit as soon as it starts (PreviewPlan) and holds the tiles of every
// leaf the plan reads: node outputs and raw wets from the engine's TileCache, the stroke's
// coverage of its own. It's also the one place that says what a reached node is under the edit:
// its scales, a Move's offset, its bounds. Its branches render it, each in its own space
// (PreviewRenderer):
//
//   canvas      PreviewItem: the master under the edit, in the canvas view (preview-item.hpp)
//   thumbnails  PreviewPicture: a reached node fitted into a picture: the graph's thumbnails,
//               and the master in the navigator (preview-picture.hpp)
//
// The rule: while an edit is in progress, its pictures are made on the GPU only; the CPU
// changes the document once it ends, and the views follow that as usual. An edit of the graph
// alone (nodes or a link being moved) isn't planned: nothing's pixels change.

#include "base/math.hpp"
#include "base/signal.hpp"
#include "canvas/preview/preview-plan.hpp"
#include "canvas/tiles/tile-cache.hpp"

#include <memory>
#include <optional>
#include <vector>

class Project;

class Preview {
public:
    void init(const Project& project, TileCache& cache);
    void shutdown();

    // Once a frame, after the cache synced: the stroke's coverage tiles follow the stroke.
    void update();

    bool active() const { return m_active; }   // planned, and the plan is the project's edit's
    const Project&     project() const { return *m_project; }
    const PreviewPlan& plan() const { return m_plan; }
    const EditPreview& edit() const;                 // while active
    RasterTiles& leaf(int i) const { return *m_leaves[size_t(i)]; }
    uint64_t planSerial() const { return m_serial; }   // bumps with every new plan: renderers start over

    // ---- a node under the edit (any node: one the edit doesn't reach is as it is)
    float scale(const Node& n, NodeSignal s, NodeChannel c) const;
    // The offset a node shifts its input by (a kind with MoveSettings), as it shows: none for others.
    std::optional<Vec2> moveOffset(const Node& n) const;
    bool  wetOn(const Node& n) const;
    bool  dryOn(const Node& n) const;
    BoundsI bounds(NodeId id) const;             // world: what its output can cover

private:
    void follow();   // plans the project's edit, or lets go of the last one
    void replan(const EditPreview& edit);
    void drop();
    BoundsI boundsOf(int node, Vec2 offset) const;
    BoundsI sourceBounds(PreviewPlan::Source src, Vec2 offset) const;
    BoundsI leafBounds(int leaf, Vec2 offset) const;

    const Project* m_project = nullptr;
    TileCache*     m_cache   = nullptr;
    Connection     m_events;              // project.changed: a new edit or a graph change replans
    PreviewPlan    m_plan;
    std::vector<std::shared_ptr<RasterTiles>> m_leaves;   // per plan leaf
    bool     m_active = false;
    bool     m_stale  = false;            // the graph changed since the plan was made
    size_t   m_kind   = 0;                // the planned edit's kind (EditPreview::edit's index)
    uint64_t m_serial = 0;
};
