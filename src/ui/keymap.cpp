#include "ui/keymap.hpp"
#include "command/actions.hpp"

#include <initializer_list>

namespace {

struct Binding {
    ActionId id;
    Shortcut keys;
};

// Primary is Cmd on macOS, Ctrl elsewhere.
const std::initializer_list<Binding> kDefaultKeys = {
    // the document
    { Actions::EditUndo,           { Key::Z, ModPrimary } },
    { Actions::EditRedo,           { Key::Z, ModPrimary | ModShift } },
    { Actions::EditRedo,           { Key::Y, ModPrimary } },
    // the app
    { Actions::DocNew,             { Key::N, ModPrimary } },
    { Actions::FileOpen,           { Key::O, ModPrimary } },
    { Actions::FileSave,           { Key::S, ModPrimary } },
    { Actions::FileSaveAs,         { Key::S, ModPrimary | ModShift } },
    { Actions::FileClose,          { Key::W, ModPrimary } },
    { Actions::FileImport,         { Key::O, ModPrimary | ModShift } },
    // tools and colours
    { Actions::ToolBrush,          { Key::B } },
    { Actions::ToolHand,           { Key::H } },
    { Actions::ToolEraser,         { Key::E } },
    { Actions::ColorSwap,          { Key::X } },
    { Actions::BrushBigger,        { Key::RightBracket } },
    { Actions::BrushSmaller,       { Key::LeftBracket } },
    // the canvas (while it has the pointer)
    { Actions::CanvasFrameAll,     { Key::F } },
    // the graph (while it has the pointer or the focus)
    { Actions::GraphDelete,        { Key::Delete } },
    // the History Tree (while it has the pointer or the focus)
    { Actions::HistoryTreeDelete,  { Key::Delete } },
    { Actions::HistoryTreeDelete,  { Key::Backspace } },
    { Actions::GraphDelete,        { Key::Backspace } },
    { Actions::GraphFrameSelected, { Key::F } },
    { Actions::GraphFrameAll,      { Key::Home } },
    { Actions::GraphSelectAll,     { Key::A, ModPrimary } },
    { Actions::GraphAddNode,       { Key::A, ModShift } },
};

}

void bindDefaultKeys(ActionRegistry& actions)
{
    for (const Binding& b : kDefaultKeys) actions.bind(b.id, b.keys);
}
