#pragma once
// What an edit in progress (Project::preview()) reaches, worked out once when it starts: the
// edited node and every node downstream of it (through inputs and masks), each rebuilt on the GPU
// every frame from the values being dragged; and the leaves they read, which the edit can't
// change, frozen as tiles:
//   - the layers of a reached stack that the edit doesn't reach: their outputs,
//   - the raw wet of reached nodes that read no input (a raster's own pixels; a stack's whole
//     composite when only its signals change),
//   - every mask the edit doesn't reach: its output,
//   - a stroke's coverage, over the raster it paints.

#include "document/edit-preview.hpp"

#include <absl/container/flat_hash_map.h>
#include <vector>

class Project;

struct PreviewLeaf {
    enum class Kind : uint8_t { Output, Wet, Stroke };
    NodePtr node;   // Stroke: the raster being painted
    Kind    kind;
};

class PreviewPlan {
public:
    // Where a reached node reads something from: a frozen leaf, or another reached node.
    struct Source {
        int leaf = -1, node = -1;
        explicit operator bool() const { return leaf >= 0 || node >= 0; }
    };
    struct Node {
        NodePtr node;
        Source  input;                                // a one-input node's (InputShape::One)
        std::vector<Source> layers;                   // a recomposed stack's, bottom first
        int     wet = -1;                             // otherwise its raw wet, frozen (leaf)
        int     stroke = -1;                          // ... with the stroke over it (leaf)
        Source  masks[kSignalCount][kChannelCount];
    };

    // False when the edited node is gone (the plan is then empty).
    bool build(const Project& project, const EditPreview& edit);
    void clear();

    NodeId edited() const { return m_edited; }
    bool   reaches(NodeId id) const { return m_index.contains(id); }
    int    indexOf(NodeId id) const;   // -1: not reached
    const std::vector<Node>&        nodes()  const { return m_nodes; }
    const std::vector<PreviewLeaf>& leaves() const { return m_leaves; }

private:
    int    leafFor(const NodePtr& n, PreviewLeaf::Kind kind);
    Source sourceFor(const NodePtr& n);
    int    planFor(const NodePtr& n);

    NodeId m_edited = kNoNodeId;
    bool   m_stroke = false;   // the edit is a stroke
    absl::flat_hash_map<const ::Node*, int> m_reached;   // node -> its plan (-1 until planned)
    absl::flat_hash_map<NodeId, int> m_index;
    std::vector<Node>        m_nodes;
    std::vector<PreviewLeaf> m_leaves;
};
