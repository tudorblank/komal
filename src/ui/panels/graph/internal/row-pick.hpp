#pragma once
// A stack's rows picked like files in a browser: a click picks the row, Ctrl adds it or takes it
// out, Shift picks every row from the anchor to it (Ctrl+Shift adds them). What a drag from the
// press carries: the rows picked, as shown (an artboard's layers go with it).
//
//   const RowPick p = pickRow(shown, picked, anchor, row, ctrl, shift);

#include "ui/panels/graph/internal/stack-drop.hpp"

#include <vector>

struct RowPick {
    std::vector<StackRowRef> picked;
    StackRowRef anchor;
    bool wasPicked = false;              // a plain click on a row of a bigger pick (a click, no drag: just it)
    std::vector<StackRowRef> carried;    // the picked rows, top first; none when Ctrl took the row out
};

// `shown`: the stack's rows as laid out, top first; `picked` / `anchor`: as they were.
RowPick pickRow(const std::vector<StackRowRef>& shown, std::vector<StackRowRef> picked, StackRowRef anchor, StackRowRef row,
                bool ctrl, bool shift);
