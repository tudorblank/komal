#include "ui/panels/graph/internal/stack-drop.hpp"
#include "ui/panels/graph/internal/node-item.hpp"
#include "ui/panels/graph/internal/layout.hpp"

#include <algorithm>

using namespace GraphLayout;

StackGap stackDrop(const NodeItem& item, float y, StackGap::Kind kind, bool carriesArtboard, const std::vector<StackRowRef>& carried)
{
    if (item.stack.lifted != carried) return {};   // laid out for another drag: no drop yet
    const StackGap& now = item.stack.gap;   // the drag as this layout shows it
    const auto& rows = item.layers;         // top first
    const auto isRow = [](const NodeItemLayerRow& r) { return r.as<NodeItemLayerRow::Layer>() || r.as<NodeItemLayerRow::Artboard>(); };
    // How many rows an array holds (the top level, or an artboard's); a row's position in its own, bottom first.
    const auto count = [&](ArtboardId inside) {
        return size_t(std::count_if(rows.begin(), rows.end(), [&](const NodeItemLayerRow& r) { return isRow(r) && r.inside == inside; }));
    };
    const auto position = [&](const NodeItemLayerRow& row) {
        size_t above = 0;
        for (const NodeItemLayerRow& r : rows)
            if (&r == &row) break;
            else if (isRow(r) && r.inside == row.inside) ++above;
        return count(row.inside) - 1 - above;
    };
    const auto headerOf = [&](ArtboardId key) -> const NodeItemLayerRow* {
        for (const NodeItemLayerRow& r : rows)
            if (const auto* a = r.as<NodeItemLayerRow::Artboard>(); a && a->key == key) return &r;
        return nullptr;
    };
    const auto bottomIsArtboard = [&] {
        for (auto r = rows.rbegin(); r != rows.rend(); ++r)
            if (isRow(*r) && r->inside == kNoArtboard) return r->as<NodeItemLayerRow::Artboard>() != nullptr;
        return false;
    };
    const auto room = [&](ArtboardId into, size_t at) {
        const StackPlace place{ into, at };
        // Where the one row carried was: back there, no room (dropped, it changes nothing).
        if (item.origin && *item.origin == place) return StackGap::home(kind, place);
        return StackGap::room(kind, place, into == kNoArtboard && at == 0 && bottomIsArtboard());
    };
    // An artboard's row has a top edge (room above it) only where nothing else gives that place:
    // under another artboard (its row, or its layers; room between them doesn't count). Under a
    // layer of the top level (its lower half gives it), or first (above every row is the top), all
    // of its row is onto it.
    const auto rowBefore = [&](const NodeItemLayerRow& row) -> const NodeItemLayerRow* {
        const NodeItemLayerRow* prev = nullptr;
        for (const NodeItemLayerRow& r : rows) {
            if (&r == &row) break;
            if (isRow(r)) prev = &r;
        }
        return prev;
    };
    const auto rowAfter = [&](const NodeItemLayerRow& row) -> const NodeItemLayerRow* {
        bool past = false;
        for (const NodeItemLayerRow& r : rows) {
            if (past && isRow(r)) return &r;
            if (&r == &row) past = true;
        }
        return nullptr;
    };
    const auto hasEdge = [&](const NodeItemLayerRow& row) {
        const NodeItemLayerRow* prev = rowBefore(row);
        return row.as<NodeItemLayerRow::Artboard>() && prev && (prev->inside != kNoArtboard || prev->as<NodeItemLayerRow::Artboard>());
    };
    // The drop going onto this row closes the room shown above it (if any): the rows under it move
    // up by the room and its gap. What was onto it then is that far down, to the middle of its gap.
    const auto closedAbove = [&](const NodeItemLayerRow& row) {
        for (const NodeItemLayerRow& r : rows) {
            if (&r == &row) break;
            if (r.as<NodeItemLayerRow::Gap>()) return r.rect.GetHeight() + kLayerGap * 1.5f;
        }
        return 0.0f;
    };
    const auto above = [&](const NodeItemLayerRow& r) { return room(r.inside, position(r) + 1); };
    const auto under = [&](const NodeItemLayerRow& r) { return room(r.inside, position(r)); };

    // The row (or the room) the height is on: the edges between rows split down the middle.
    const NodeItemLayerRow* on = nullptr;
    for (const NodeItemLayerRow& r : rows) {
        if (!isRow(r) && !r.as<NodeItemLayerRow::Gap>() && !r.as<NodeItemLayerRow::Origin>()) continue;
        on = &r;
        if (y <= r.rect.Max.y + kLayerGap * 0.5f) break;
    }
    if (!on) return room(kNoArtboard, 0);                               // an empty stack
    // Onto a folded or empty artboard already: it holds as far under its row as the rows moved up
    // when it took the drop (StackGap::hold), never over the next artboard's top edge.
    if (now.artboard() != kNoArtboard && !carriesArtboard)
        if (const NodeItemLayerRow* h = headerOf(now.artboard()); h && h->as<NodeItemLayerRow::Artboard>()) {
            const auto* a = h->as<NodeItemLayerRow::Artboard>();
            const NodeItemLayerRow* next = rowAfter(*h);
            const float down = (a->folded || a->layers == 0) && !(next && hasEdge(*next)) ? now.hold() : 0.0f;
            const float top  = hasEdge(*h) ? h->rect.Min.y + h->rect.GetHeight() * kArtboardEdge : h->rect.Min.y;
            if (y >= top && y <= h->rect.Max.y + down) return now.as(kind);
        }
    if (y > on->rect.Max.y + kLayerGap * 0.5f) return room(kNoArtboard, 0);                    // below every row: the very bottom
    if (y < on->rect.Min.y - kLayerGap * 0.5f) return room(kNoArtboard, count(kNoArtboard));   // above every row: the top
    if (on->as<NodeItemLayerRow::Gap>()) return now.as(kind);   // on the room (only an opening drop shows one): its place again
    if (const auto* o = on->as<NodeItemLayerRow::Origin>()) {   // a carried row's spot: where it was (the rows left under it)
        const NodeItemLayerRow* spot = on;   // a carried artboard's: its row's
        if (o->artboard != kNoArtboard)
            for (const NodeItemLayerRow& r : rows)
                if (const auto* x = r.as<NodeItemLayerRow::Origin>(); x && x->artboard == o->artboard && r.inside == kNoArtboard) { spot = &r; break; }
        size_t rowsUnder = 0;
        bool after = false;
        for (const NodeItemLayerRow& r : rows) {
            if (after && isRow(r) && r.inside == spot->inside) ++rowsUnder;
            if (&r == spot) after = true;
        }
        return room(spot->inside, rowsUnder);
    }
    const bool upper = y < on->rect.GetCenter().y;

    if (const auto* a = on->as<NodeItemLayerRow::Artboard>()) {
        if (carriesArtboard) return upper ? above(*on) : under(*on);   // an artboard beside it, never in it
        if (hasEdge(*on) && y < on->rect.Min.y + on->rect.GetHeight() * kArtboardEdge) return above(*on);   // its top edge
        return StackGap::onto(kind, a->key, closedAbove(*on));   // onto it: no room
    }
    if (on->inside != kNoArtboard && carriesArtboard) {   // an artboard over another's layers: beside it
        const NodeItemLayerRow* header = headerOf(on->inside);
        if (!header) return room(kNoArtboard, 0);
        float bottom = header->rect.Max.y;   // the artboard's whole block: its row and its layers
        for (const NodeItemLayerRow& r : rows)
            if (r.inside == on->inside) bottom = std::max(bottom, r.rect.Max.y);
        return y < (header->rect.Min.y + bottom) * 0.5f ? above(*header) : under(*header);
    }
    return upper ? above(*on) : under(*on);   // in its own array
}

StackGap settleDrop(StackGap drop, const std::function<NodeItem(const StackGap&)>& layoutOf, float y, StackGap::Kind kind,
                    bool carriesArtboard, const std::vector<StackRowRef>& carried)
{
    for (int i = 0; i < 4 && drop.dropping(); ++i) {   // a room closing moves rows once; never more than a few
        const StackGap again = stackDrop(layoutOf(drop), y, kind, carriesArtboard, carried);
        if (again == drop) break;
        drop = again;
    }
    return drop;
}

StackPlace landing(const NodeItem& item, const StackGap& gap)
{
    if (gap.target() == StackGap::Target::Home && item.origin) return *item.origin;   // where it was: nothing changes
    if (gap.artboard() != kNoArtboard)   // on top of its layers
        for (const NodeItemLayerRow& r : item.layers)
            if (const auto* a = r.as<NodeItemLayerRow::Artboard>(); a && a->key == gap.artboard()) return { a->key, a->layers };
    return gap.place();
}
