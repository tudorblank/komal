#pragma once
// The edit in progress (Project::preview()) on the CPU: what it reaches (PreviewPlan) and what a
// node is under it: its scales, a Move's offset, whether its wet and dry show, its bounds. The one
// place these rules live: the GPU preview (canvas/preview) renders from them, and tests hold them
// to what the document becomes once the edit is applied.
//
//   PreviewModel model(project);
//   if (model.plan(*project.preview())) ... model.bounds(id), model.scale(node, s, c)

#include "base/math.hpp"
#include "document/preview-plan.hpp"

#include <optional>

class Project;

class PreviewModel {
public:
    explicit PreviewModel(const Project& project) : m_project(&project) {}

    bool plan(const EditPreview& edit) { return m_plan.build(*m_project, edit); }   // false: the edited node is gone
    void clear() { m_plan.clear(); }
    const PreviewPlan& plan() const { return m_plan; }

    // ---- a node under the project's edit (one the edit doesn't touch is as it is)
    float scale(const Node& n, NodeSignal s, NodeChannel c) const;
    // The offset a node shifts its input by (a kind with MoveSettings), as it shows: none for others.
    std::optional<Vec2> moveOffset(const Node& n) const;
    bool wetOn(const Node& n) const;
    bool dryOn(const Node& n) const;
    BoundsI bounds(NodeId id) const;   // world: what its output can cover (reached: as planned)

private:
    BoundsI boundsOf(int node, Vec2 offset) const;
    BoundsI sourceBounds(PreviewPlan::Source src, Vec2 offset) const;
    BoundsI leafBounds(int leaf, Vec2 offset) const;

    const Project* m_project;
    PreviewPlan    m_plan;
};
