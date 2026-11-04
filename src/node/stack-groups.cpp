#include "node/stack-groups.hpp"

#include <algorithm>

namespace StackGroups {

std::vector<Entry> entries(const std::vector<StackGroup>& groups, size_t rows)
{
    // Each group where it begins, the layers between them.
    std::vector<Entry> out;
    size_t row = 0, g = 0;
    for (;;) {
        if (g < groups.size() && groups[g].begin <= row) {
            const StackGroup& gr = groups[g++];
            const uint32_t end = uint32_t(std::min<size_t>(gr.end, rows));
            out.push_back({ gr.key, std::min(gr.begin, end), end });
            row = std::max<size_t>(row, gr.end);
            continue;
        }
        if (row >= rows) break;
        out.push_back({ kNoGroup, uint32_t(row), uint32_t(row + 1) });
        ++row;
    }
    return out;
}

std::vector<StackGroup> normalized(const std::vector<StackGroup>& groups, size_t rows)
{
    std::vector<StackGroup> out;
    uint32_t floor = 0;
    for (StackGroup g : groups) {
        if (g.key == kNoGroup || std::any_of(out.begin(), out.end(), [&](const StackGroup& o) { return o.key == g.key; })) continue;
        g.begin = std::clamp(g.begin, floor, uint32_t(rows));
        g.end   = std::clamp(g.end, g.begin, uint32_t(rows));
        floor = g.end;
        out.push_back(g);
    }
    return out;
}

int groupOf(const std::vector<StackGroup>& groups, size_t row)
{
    for (size_t g = 0; g < groups.size(); ++g)
        if (groups[g].begin <= row && row < groups[g].end) return int(g);
    return -1;
}

void rowInserted(std::vector<StackGroup>& groups, size_t at, int prefer)
{
    int join = -1;
    for (size_t g = 0; g < groups.size(); ++g)
        if (groups[g].begin < at && at < groups[g].end) join = int(g);
    if (join < 0 && prefer >= 0 && groups[size_t(prefer)].begin <= at && at <= groups[size_t(prefer)].end) join = prefer;
    for (size_t g = 0; g < groups.size(); ++g) {
        StackGroup& gr = groups[g];
        if (int(g) == join) {
            ++gr.end;
        } else if (join >= 0 ? int(g) > join : gr.begin >= at) {   // above it
            ++gr.begin;
            ++gr.end;
        }
    }
}

void rowRemoved(std::vector<StackGroup>& groups, size_t at)
{
    for (StackGroup& g : groups) {
        if (g.begin <= at && at < g.end) --g.end;
        else if (g.begin > at) { --g.begin; --g.end; }
    }
}

void rowMoved(std::vector<StackGroup>& groups, size_t from, size_t to)
{
    const int left = groupOf(groups, from);
    rowRemoved(groups, from);
    rowInserted(groups, to, left);
}

void rowsCleared(std::vector<StackGroup>& groups)
{
    for (StackGroup& g : groups) g.begin = g.end = 0;
}

}
