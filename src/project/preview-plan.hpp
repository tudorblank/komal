#pragma once
// What an edit in progress (Workspace::preview()) reaches, worked out once when it starts: the
// edited node and every node downstream of it (through inputs and masks), each rebuilt on the GPU
// every frame from the values being dragged; and the leaves they read, which the edit can't
// change, frozen as tiles:
//   - the layers of a reached stack that the edit doesn't reach: their outputs (a stack is
//     recomposed as it draws: its layers shown, its groups' fills between them),
//   - the raw wet of reached nodes that read no input (a raster's own pixels; a stack's whole
//     composite when only its signals change),
//   - every mask the edit doesn't reach: its output,
//   - a stroke's coverage, over the raster it paints.
// How each reached node's wet is made is decided here, once, from its kind's WetRule
// (node-kinds.hpp): the GPU preview and the bounds rules both switch on it.

#include "project/edit-preview.hpp"

#include <absl/container/flat_hash_map.h>
#include <vector>

class Workspace;

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
    struct Fill {
        BoundsI rect;
        Color8  color;    // straight
        size_t  under;    // drawn under layers[under] (layers.size(): on top)
    };
    // How a reached node's wet is made under the edit.
    enum class Wet : uint8_t {
        Input,    // its input as it is
        Moved,    // its input, shifted by its offset (PreviewModel::moveOffset)
        Stack,    // recomposed: its layers and fills, as it draws
        Frozen,   // as it was (a leaf): its own pixels, a stack the edit reaches only through
                  // its signals, or a kind the GPU can't rebuild (WetRule::Computed)
    };
    struct Planned {
        NodePtr node;
        Wet     how = Wet::Frozen;
        Source  input;                                // a one-input node's, when its wet or dry reads it
        std::vector<Source> layers;                   // Stack: its layers shown, bottom first ...
        std::vector<Fill>   fills;                    // ... and its groups' fills
        int     wet = -1;                             // Frozen: its raw wet (leaf) ...
        int     stroke = -1;                          // ... with the stroke over it (leaf)
        Source  masks[kSignalCount][kChannelCount];
    };

    // False when the edited node is gone (the plan is then empty).
    bool build(const Workspace& workspace, const EditPreview& edit);
    void clear();

    NodeId edited() const { return m_edited; }
    bool   reaches(NodeId id) const { return m_index.contains(id); }
    int    indexOf(NodeId id) const;   // -1: not reached
    const std::vector<Planned>&     nodes()  const { return m_nodes; }
    const std::vector<PreviewLeaf>& leaves() const { return m_leaves; }

private:
    int    leafFor(const NodePtr& n, PreviewLeaf::Kind kind);
    Source sourceFor(const NodePtr& n);
    int    planFor(const NodePtr& n);

    NodeId m_edited = kNoNodeId;
    bool   m_stroke = false;   // the edit is a stroke
    absl::flat_hash_map<NodeId, int> m_index;   // every node reached -> its plan (-1 until planned)
    std::vector<Planned>     m_nodes;
    std::vector<PreviewLeaf> m_leaves;
};
