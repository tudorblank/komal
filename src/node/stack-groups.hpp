#pragma once
// A stack's groups: runs of its rows that belong together (the master's artboards). The one place
// their rules live: what the top level of a stack is, and how groups follow rows coming, going
// and moving. The compositor keeps its groups with these; the document moves rows and the graph
// lays them out from the same walk (entries), so all three always agree.
//
//   rows (bottom first)   0  1  2  3  4
//   group 7 = [1, 3)         └──┘
//   entries               0  [7: 1 2]  3  4

#include <cstddef>
#include <cstdint>
#include <vector>

enum class GroupKey : uint32_t {};   // whose the group is (the document's: an artboard's id); its own type
constexpr GroupKey kNoGroup{};

// [begin, end), bottom first; an empty one sits at `begin`.
struct StackGroup {
    GroupKey key = kNoGroup;
    uint32_t begin = 0, end = 0;
    bool operator==(const StackGroup&) const = default;
};

namespace StackGroups {

// One row at the top level, bottom first: a layer of its own ([begin, begin + 1), no group), or a
// group with its rows.
struct Entry {
    GroupKey group = kNoGroup;
    uint32_t begin = 0, end = 0;
    bool operator==(const Entry&) const = default;
};
std::vector<Entry> entries(const std::vector<StackGroup>& groups, size_t rows);

// Groups as a stack can hold them, for `rows` rows: each key once (none without one), in order,
// in range, apart.
std::vector<StackGroup> normalized(const std::vector<StackGroup>& groups, size_t rows);

int  groupOf(const std::vector<StackGroup>& groups, size_t row);   // the index of the group holding it, -1: none
// A row came in at `at`: strictly inside a group it joins it (so does it at the edges of the group
// `prefer`, the one it left when it's moving); else it's on its own, under any group starting there.
void rowInserted(std::vector<StackGroup>& groups, size_t at, int prefer = -1);
void rowRemoved(std::vector<StackGroup>& groups, size_t at);
void rowMoved(std::vector<StackGroup>& groups, size_t from, size_t to);   // it keeps its group at that group's edges
void rowsCleared(std::vector<StackGroup>& groups);

}
