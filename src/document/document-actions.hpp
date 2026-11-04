#pragma once
// Every way the project can change, as actions: the one list. Panels and tools run these;
// only these call the Project's editing API. Each document edit is one undo step, but moving
// through history (undo, redo, history.goTo) and picking the raster to paint into (raster.activate).
//
//   document edits   edit.undo  edit.redo  history.goTo
//                    node.add  node.reference  node.delete  node.rename
//                    raster.activate  raster.lock  raster.lockAlpha
//                    link.connect  link.disconnect  layer.move  layers.move  layer.add  layers.remove  layer.mute
//                    artboard.add  artboard.set  artboard.delete
//                    signal.set  move.set  image.import
//   gestures         brush.stroke  node.drag  signal.scrub  move.scrub  link.move
//   what views share selection.set
//   (never undone)   canvas.viewport  canvas.pan  canvas.zoom  canvas.rotate  canvas.centerOn
//                    canvas.frame  canvas.frameAll
//
// Their arguments are the keys below: nodes by id, node kinds by NodeKind::id, slots as
// InputSlot::text() writes them. Payloads (bulk data, not logged):
//   brush.stroke   update: std::span<const StrokePoint>
//   image.import   const TileGrid*

#include "command/actions.hpp"
#include "document/artboard.hpp"
#include "document/history.hpp"
#include "node/input-slot.hpp"
#include "document/node-selection.hpp"
#include "node/node.hpp"
#include "raster/brush.hpp"

#include <string>
#include <vector>

class Project;

namespace DocArgs {

// Nodes, and where links land.
inline constexpr ArgKey<NodeId>              kNode{ "node" };
inline constexpr ArgKey<std::vector<NodeId>> kNodes{ "nodes" };
inline constexpr ArgKey<NodeId>              kFrom{ "from" };     // a link's source, or what a new node is fed from
inline constexpr ArgKey<NodeId>              kTo{ "to" };         // a link's target
inline constexpr ArgKey<NodeId>              kInto{ "into" };     // what a new node feeds
inline constexpr ArgKey<NodeId>              kSource{ "source" }; // a lifted link's source
inline constexpr ArgKey<InputSlot>           kSlot{ "slot" };
inline constexpr ArgKey<InputSlot>           kToSlot{ "toSlot" };
inline constexpr ArgKey<bool>                kDropped{ "dropped" };   // link.move: dropped on nothing
inline constexpr ArgKey<std::string>         kType{ "type" };     // a NodeKind::id
inline constexpr ArgKey<std::string>         kLabel{ "label" };
inline constexpr ArgKey<NodeId>              kAnchor{ "anchor" };   // selection.set: what Shift+click chains from

// Places: a node's in the graph, a point on the canvas.
inline constexpr ArgKey<float> kX{ "x" };
inline constexpr ArgKey<float> kY{ "y" };

// History, layers, values.
inline constexpr ArgKey<StepId>      kStep{ "step" };
inline constexpr ArgKey<size_t>      kLayerFrom{ "from" };   // layer.move: a layer's index (0 = bottom)
inline constexpr ArgKey<size_t>      kLayerTo{ "to" };       // ... where it goes; layers.move: a place among the rows left
inline constexpr ArgKey<NodeId>      kLayer{ "layer" };      // layer.mute: one of the compositor's layers
inline constexpr ArgKey<std::vector<NodeId>>     kLayers{ "layers" };        // layers.move: the layers moved ...
inline constexpr ArgKey<std::vector<ArtboardId>> kArtboards{ "artboards" };  // ... and artboards, whole (the master's)
inline constexpr ArgKey<ArtboardId>  kArtboard{ "artboard" };
inline constexpr ArgKey<ArtboardId>  kIntoArtboard{ "into" };   // layers.move, layer.add, link.move: into this artboard (else the top level)
inline constexpr ArgKey<size_t>      kPlaceAt{ "at" };          // layer.add, link.move: a place among the stack's rows (see StackMove)
inline constexpr ArgKey<bool>        kVisible{ "visible" };     // artboard.set: its eye
inline constexpr ArgKey<bool>        kOn{ "on" };
inline constexpr ArgKey<NodeSignal>  kSignal{ "signal" };
inline constexpr ArgKey<NodeChannel> kChannel{ "channel" };
inline constexpr ArgKey<float>       kValue{ "value" };   // a signal's scale, 0..1
inline constexpr ArgKey<int>         kOffsetX{ "x" };     // a Move's offset: whole pixels
inline constexpr ArgKey<int>         kOffsetY{ "y" };
inline constexpr ArgKey<float>       kDx{ "dx" };
inline constexpr ArgKey<float>       kDy{ "dy" };

// The canvas camera.
inline constexpr ArgKey<float> kWidth{ "width" };
inline constexpr ArgKey<float> kHeight{ "height" };
inline constexpr ArgKey<float> kFactor{ "factor" };
inline constexpr ArgKey<float> kDegrees{ "degrees" };
inline constexpr ArgKey<int>   kMinX{ "minX" };
inline constexpr ArgKey<int>   kMinY{ "minY" };
inline constexpr ArgKey<int>   kMaxX{ "maxX" };
inline constexpr ArgKey<int>   kMaxY{ "maxY" };

// image.import
inline constexpr ArgKey<std::string> kPath{ "path" };
inline constexpr ArgKey<std::string> kName{ "name" };
inline constexpr ArgKey<std::string> kTarget{ "target" };   // where it was dropped: default, canvas, graph

// brush.stroke's begin: everything the stroke is made from (strokeArgs()), and its end.
inline constexpr ArgKey<NodeId>      kRaster{ "raster" };     // the active raster, painted into
inline constexpr ArgKey<float>       kPressure{ "pressure" };  // the first point's (x y too)
inline constexpr ArgKey<std::string> kDevice{ "device" };     // mouse, pen
inline constexpr ArgKey<std::string> kMode{ "mode" };         // paint, erase
inline constexpr ArgKey<float>       kSize{ "size" };
inline constexpr ArgKey<float>       kHardness{ "hardness" };
inline constexpr ArgKey<float>       kFlow{ "flow" };
inline constexpr ArgKey<float>       kSpacing{ "spacing" };
inline constexpr ArgKey<uint8_t>     kOpacity{ "opacity" };
inline constexpr ArgKey<Color8>      kColor{ "color" };
inline constexpr ArgKey<bool>        kAntialias{ "antialias" };
inline constexpr ArgKey<bool>        kPressureSize{ "pressureSize" };
inline constexpr ArgKey<bool>        kPressureFlow{ "pressureFlow" };
inline constexpr ArgKey<int>         kPoints{ "points" };   // end: how many points it took

}

void registerDocumentActions(ActionRegistry& actions, Project& project);

ActionArgs selectionArgs(const NodeSelection& selection);   // selection.set's, for that selection
// brush.stroke's begin, for a stroke made of these (keepAlpha comes from the raster's lock).
ActionArgs strokeArgs(NodeId raster, const BrushParams& brush, StrokePoint at, bool pen);
