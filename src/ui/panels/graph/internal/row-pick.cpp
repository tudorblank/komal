#include "ui/panels/graph/internal/row-pick.hpp"

#include <algorithm>

RowPick pickRow(const std::vector<StackRowRef>& shown, std::vector<StackRowRef> picked, StackRowRef anchor, StackRowRef row,
                bool ctrl, bool shift)
{
    const auto has = [](const std::vector<StackRowRef>& v, StackRowRef r) { return std::find(v.begin(), v.end(), r) != v.end(); };
    RowPick out;
    if (shift) {
        const auto at = [&](StackRowRef r) { return std::find(shown.begin(), shown.end(), r) - shown.begin(); };
        if (anchor.empty() || !has(shown, anchor)) anchor = row;
        const std::ptrdiff_t from = at(anchor), to = at(row);
        const std::ptrdiff_t lo = std::min(from, to), hi = std::max(from, to);
        if (!ctrl) picked.clear();
        for (auto i = lo; i <= hi && i < std::ptrdiff_t(shown.size()); ++i)
            if (!has(picked, shown[size_t(i)])) picked.push_back(shown[size_t(i)]);
    } else if (ctrl) {
        if (has(picked, row)) std::erase(picked, row);
        else picked.push_back(row);
        anchor = row;
    } else {
        out.wasPicked = has(picked, row) && picked.size() > 1;
        if (!has(picked, row)) picked = { row };
        anchor = row;
    }
    if (has(picked, row))
        for (const StackRowRef& r : shown)
            if (has(picked, r)) out.carried.push_back(r);
    out.picked = std::move(picked);
    out.anchor = anchor;
    return out;
}
