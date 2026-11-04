#pragma once
// The edit in progress (Workspace::preview()) as the GPU shows it: one for the whole app, in the
// canvas engine. It plans the edit as soon as it starts (its PreviewModel: the plan, and what a
// reached node is under the edit, on the CPU) and holds the tiles of every leaf the plan reads:
// node outputs and raw wets from the engine's TileCache, the stroke's coverage of its own. Its
// branches render it, each in its own space (PreviewRenderer):
//
//   canvas      PreviewItem: the master under the edit, in the canvas view (preview-item.hpp)
//   thumbnails  PreviewPicture: a reached node fitted into a picture: the graph's thumbnails,
//               and the master in the navigator (preview-picture.hpp)
//
// The rule: while an edit is in progress, its pictures are made on the GPU only; the CPU
// changes the document once it ends, and the views follow that as usual. An edit of the graph
// alone (nodes or a link being moved) isn't planned: nothing's pixels change.

#include "base/signal.hpp"
#include "ui/panels/canvas/tiles/tile-cache.hpp"
#include "document/preview-model.hpp"

#include <memory>
#include <optional>
#include <vector>

class Workspace;

class Preview {
public:
    void init(const Workspace& workspace, TileCache& cache);
    void shutdown();
    void releaseGpu();   // its tiles go; the edit in progress is planned again

    // Once a frame, after the cache synced: the stroke's coverage tiles follow the stroke.
    void update();

    bool active() const { return m_active; }   // planned, and the plan is the workspace's edit's
    const Workspace&      workspace() const { return *m_workspace; }
    const PreviewModel& model() const { return *m_model; }   // the plan, and every node under the edit
    const PreviewPlan&  plan() const { return m_model->plan(); }
    const EditPreview&  edit() const;                 // while active
    RasterTiles& leaf(int i) const { return *m_leaves[size_t(i)]; }
    uint64_t planSerial() const { return m_serial; }   // bumps with every new plan: renderers start over

private:
    void follow();   // plans the workspace's edit, or lets go of the last one
    void replan(const EditPreview& edit);
    void drop();

    const Workspace* m_workspace = nullptr;
    TileCache*     m_cache   = nullptr;
    Connection     m_events;              // workspace.changed: a new edit or a graph change replans
    std::optional<PreviewModel> m_model;  // from init()
    std::vector<std::shared_ptr<RasterTiles>> m_leaves;   // per plan leaf
    bool     m_active = false;
    bool     m_stale  = false;            // the graph changed since the plan was made
    size_t   m_kind   = 0;                // the planned edit's kind (EditPreview::edit's index)
    uint64_t m_serial = 0;
};
