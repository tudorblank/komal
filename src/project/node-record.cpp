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

NodeRecord::Applied NodeRecord::applyTo(GraphKey key, Node& n) const
{
    Applied a;
    if (n.x() != x || n.y() != y) {
        n.setPosition(key, x, y);
        a.moved = true;
    }
    if (n.label() != label) { n.setLabel(key, label); a.changed = true; }
    for (int sig = 0; sig < kSignalCount; ++sig)
        for (int ch = 0; ch < kChannelCount; ++ch)
            if (n.hasSignal(NodeSignal(sig)) && n.signal(NodeSignal(sig)).scale[ch] != scale[sig][ch]) {
                n.setScale(key, NodeSignal(sig), NodeChannel(ch), scale[sig][ch]);
                a.changed = true;
            }
    if (n.settings() != settings) {
        n.setSettings(key, settings);
        a.changed = true;
    }
    return a;
}
