#pragma once
// Every action's name, once. Owners register an action under its name here; everyone else runs
// it by the same name, so a misspelt one doesn't compile. A name is also what menus, shortcuts and
// the log show ("node.add").

#include <string_view>

class ActionId {
public:
    constexpr ActionId() = default;   // none: no action has it
    constexpr explicit ActionId(std::string_view name) : m_name(name) {}
    constexpr std::string_view name() const { return m_name; }
    constexpr bool operator==(const ActionId&) const = default;

private:
    std::string_view m_name;   // a literal: lives as long as the app
};

namespace Actions {

// ---- the document (document/document-actions)
inline constexpr ActionId EditUndo{ "edit.undo" };
inline constexpr ActionId EditRedo{ "edit.redo" };
inline constexpr ActionId HistoryGoTo{ "history.goTo" };
inline constexpr ActionId NodeAdd{ "node.add" };
inline constexpr ActionId NodeReference{ "node.reference" };
inline constexpr ActionId NodeDelete{ "node.delete" };
inline constexpr ActionId NodeRename{ "node.rename" };
inline constexpr ActionId NodeDrag{ "node.drag" };
inline constexpr ActionId RasterActivate{ "raster.activate" };
inline constexpr ActionId RasterLock{ "raster.lock" };
inline constexpr ActionId RasterLockAlpha{ "raster.lockAlpha" };
inline constexpr ActionId LinkConnect{ "link.connect" };
inline constexpr ActionId LinkDisconnect{ "link.disconnect" };
inline constexpr ActionId LinkMove{ "link.move" };
inline constexpr ActionId LayerMove{ "layer.move" };
inline constexpr ActionId SignalSet{ "signal.set" };
inline constexpr ActionId SignalScrub{ "signal.scrub" };
inline constexpr ActionId MoveSet{ "move.set" };
inline constexpr ActionId MoveScrub{ "move.scrub" };
inline constexpr ActionId ImageImport{ "image.import" };
inline constexpr ActionId BrushStroke{ "brush.stroke" };

// ---- what views share, kept in the project (document/document-actions): never undone
inline constexpr ActionId SelectionSet{ "selection.set" };
inline constexpr ActionId CanvasViewport{ "canvas.viewport" };
inline constexpr ActionId CanvasPan{ "canvas.pan" };
inline constexpr ActionId CanvasZoom{ "canvas.zoom" };
inline constexpr ActionId CanvasRotate{ "canvas.rotate" };
inline constexpr ActionId CanvasCenterOn{ "canvas.centerOn" };
inline constexpr ActionId CanvasFrame{ "canvas.frame" };
inline constexpr ActionId CanvasFrameAll{ "canvas.frameAll" };

// ---- the app (ui/app-actions)
inline constexpr ActionId AppQuit{ "app.quit" };
inline constexpr ActionId FileImport{ "file.import" };
inline constexpr ActionId ViewResetLayout{ "view.resetLayout" };
inline constexpr ActionId ViewCanvas{ "view.canvas" };   // each panel's toggle (PanelInfo::toggle)
inline constexpr ActionId ViewGraph{ "view.graph" };
inline constexpr ActionId ViewTools{ "view.tools" };
inline constexpr ActionId ViewNavigator{ "view.navigator" };
inline constexpr ActionId ViewColor{ "view.color" };
inline constexpr ActionId ViewToolProperties{ "view.toolProperties" };
inline constexpr ActionId ViewHistoryTree{ "view.historyTree" };
inline constexpr ActionId ViewInfo{ "view.info" };
inline constexpr ActionId ViewImguiDemo{ "view.imguiDemo" };

// ---- tools (ui/tools)
inline constexpr ActionId ToolBrush{ "tool.brush" };   // each tool's pick (Tool::pickAction)
inline constexpr ActionId ToolHand{ "tool.hand" };
inline constexpr ActionId ToolEraser{ "tool.eraser" };
inline constexpr ActionId BrushSize{ "brush.size" };
inline constexpr ActionId BrushHardness{ "brush.hardness" };
inline constexpr ActionId BrushOpacity{ "brush.opacity" };
inline constexpr ActionId BrushFlow{ "brush.flow" };
inline constexpr ActionId BrushSpacing{ "brush.spacing" };
inline constexpr ActionId BrushAntialias{ "brush.antialias" };
inline constexpr ActionId BrushPressureSize{ "brush.pressureSize" };
inline constexpr ActionId BrushPressureFlow{ "brush.pressureFlow" };
inline constexpr ActionId BrushMode{ "brush.mode" };
inline constexpr ActionId BrushBigger{ "brush.bigger" };
inline constexpr ActionId BrushSmaller{ "brush.smaller" };
inline constexpr ActionId ColorSet{ "color.set" };
inline constexpr ActionId ColorSwap{ "color.swap" };
inline constexpr ActionId ColorEdit{ "color.edit" };   // the Color panel's: open it, in front

// ---- the graph panel's (ui/graph-panel)
inline constexpr ActionId GraphDelete{ "graph.delete" };
inline constexpr ActionId GraphFrameSelected{ "graph.frameSelected" };
inline constexpr ActionId GraphFrameAll{ "graph.frameAll" };
inline constexpr ActionId GraphSelectAll{ "graph.selectAll" };
inline constexpr ActionId GraphAddNode{ "graph.addNode" };
inline constexpr ActionId GraphShowInCanvas{ "graph.showInCanvas" };
inline constexpr ActionId GraphRename{ "graph.rename" };

}
