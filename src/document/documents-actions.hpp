#pragma once
// The open documents, as actions (Documents changes only through these): logged, never undone.
//
//   doc.new        a new blank one, active (the active one is blank already: it stays). made: its id
//   doc.close      one (the active one if none is given). An unsaved one is refused unless
//                  discard is on: whoever drives it asks first (the interface: file.close)
//   doc.activate   another one active
//   doc.save       one (the active one if none is given) to its file, or to `path` (then that's
//                  its file). Refused, with why, if it can't be written
//   doc.open       a .komal file, active (one already open: that one, active). made: its id.
//                  Refused, with why, if it can't be read
//
// None of them while the active one has an edit in progress (a stroke, a drag): it ends first.

#include "command/actions.hpp"
#include "document/document-actions.hpp"   // DocArgs::kPath: doc.save's and doc.open's file too
#include "document/document.hpp"

class Documents;

namespace DocArgs {
inline constexpr ArgKey<DocumentId> kDocument{ "document" };
inline constexpr ArgKey<bool>       kDiscard{ "discard" };   // doc.close: its unsaved changes too
}

void registerDocumentsActions(ActionRegistry& actions, Documents& documents);
