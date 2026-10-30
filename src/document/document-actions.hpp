#pragma once
// Every way the project can change, as actions: the one list. Panels and tools run these;
// only these call the Project's editing API. Each document edit is one undo step, but moving
// through history (undo, redo, history.goTo) and picking the raster to paint into (raster.activate).
//
//   document edits   edit.undo  edit.redo  history.goTo
//                    node.add  node.reference  node.delete  node.rename
//                    raster.activate  raster.lock  raster.lockAlpha
//                    link.connect  link.disconnect  layer.move
//                    signal.set  move.set  image.import
//   gestures         brush.stroke  node.drag  signal.scrub  move.scrub  link.move
//   what views share selection.set
//   (never undone)   canvas.viewport  canvas.pan  canvas.zoom  canvas.rotate  canvas.centerOn
//                    canvas.frame  canvas.frameAll
//
// Arguments name nodes by id, node kinds by NodeKind::id and slots as InputSlot::text() writes
// them. Payloads (not logged):
//   brush.stroke   begin: StrokeStart; update: std::span<const StrokePoint>
//   image.import   const TileGrid*

#include "command/actions.hpp"
#include "document/node-selection.hpp"
#include "node/node.hpp"
#include "raster/brush.hpp"

class Project;

struct StrokeStart {
    BrushParams params;
    StrokePoint at;
};

void registerDocumentActions(ActionRegistry& actions, Project& project);

ActionArgs selectionArgs(const NodeSelection& selection);   // selection.set's, for that selection
