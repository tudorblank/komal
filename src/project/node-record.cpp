#include "project/node-record.hpp"

NodeRecord NodeRecord::of(const Node& n)
{
    NodeRecord r;
    r.label = n.label();
    r.x = n.x();
    r.y = n.y();
    const NodeLinks& in = n.links();
    for (int sig = 0; sig < kSignalCount; ++sig)
        for (int ch = 0; ch < kChannelCount; ++ch) {
            r.scale[sig][ch] = n.signal(NodeSignal(sig)).scale[ch];
            r.mask[sig][ch]  = in.masks[sig][ch] ? in.masks[sig][ch]->id() : kNoNodeId;
        }
    if (in.input) r.input = in.input->id();
    for (const NodePtr& l : in.layers) r.layers.push_back(l->id());
    r.settings = n.settings();
    return r;
}
