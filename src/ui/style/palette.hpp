#pragma once
// Every colour the app's look is made of, by its job: the one source of truth. Each is made from
// the theme in use (theme.hpp, a theme file), with an alpha where it's see-through; no colour is
// written out here or anywhere else. Components don't read these directly: their styles do
// (ui-style.cpp for ImGui's own widgets and the app's controls, graph-style.cpp for the graph view,
// Style::canvas() for the canvas engine). Change a job here, or a colour in a theme file, and
// everything made of it follows.
//
//   const Palette& p = Palette::current();

#include "base/color.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct Palette {
    // ---- panels: their surfaces and text (ImGui's theme is made of these too)
    Color8 text;
    Color8 textDim;                     // disabled, hints
    Color8 panel;                       // a panel's background, its tab while shown
    Color8 bar;                         // title and menu bars, tabs
    Color8 popup;
    Color8 border;                      // around panels and popups, separators
    Color8 field;                       // value fields
    Color8 fieldHot;
    Color8 fieldActive;
    Color8 button;
    Color8 buttonHot;
    Color8 buttonActive;
    Color8 row;                         // a selected row in a list; a tab under the mouse
    Color8 rowHot;
    Color8 rowActive;
    Color8 rowAlt;                      // every other table row
    Color8 tableHeader;
    Color8 tableBorder;
    Color8 tableLine;
    Color8 scrollGrab;
    Color8 scrollGrabHot;
    Color8 scrollGrabActive;
    Color8 resizeGrip;
    Color8 plot;
    Color8 navHighlight;                // the window picked with Ctrl+Tab
    Color8 navDim;                      // ... and behind it
    Color8 modalDim;                    // behind a modal window
    Color8 none;

    // ---- the accent: selections, focus, checked things, the parts a value covers
    Color8 accent;                      // #8CB4F5
    Color8 accentDim;                   // a slider's filled part

    // ---- sliders and toggles
    Color8 track;                       // on a panel
    Color8 nodeTrack;                   // on a node
    Color8 grab;
    Color8 grabHot;
    Color8 grabRim;
    Color8 toggleEdge;
    Color8 toggleHot;
    Color8 checkMark;                   // drawn on the accent

    // ---- colour boxes and markers: readable on any colour
    Color8 darkRim;                     // around a swatch, a marker's outer ring
    Color8 lightRim;                    // inside a swatch: a black one still shows
    Color8 marker;                      // a marker's inner ring
    Color8 white;                       // the colour picker's square: white to the hue ...
    Color8 black;                       // ... and down to black
    Color8 clear;

    // ---- behind everything
    Color8 windowBackground;            // the window, where no panel is (docking's empty space too)
    Color8 canvasBackground;            // a canvas view, before its checkerboard

    // ---- pictures: behind their transparent parts
    Color8 canvasCheckerA;              // the canvas, at its origin
    Color8 canvasCheckerB;
    float  canvasChecker = 8.0f;        // its squares, points (they keep their size on screen)
    Color8 pictureCheckerA;             // small pictures (navigator, thumbnails), at a corner
    Color8 pictureCheckerB;
    float  pictureChecker = 8.0f;       // their squares, pixels

    // ---- marks over a picture
    Color8 viewOutline;                 // what the canvas shows, on a map of the picture
    Color8 emptyArea;                   // a picture not there yet
    Color8 faintOutline;                // where a dragged edit's area was
    Color8 originMark;                  // the "+" on the canvas's 0, 0
    Color8 hudBack;                     // a status line over the canvas
    Color8 hudText;

    // ---- artboards on the canvas
    Color8 artboardFrame;                   // its rect
    Color8 artboardLabel;                   // its name, above it

    // ---- notices: something went wrong
    Color8 noticeBack;
    Color8 noticeMark;

    // ---- the graph: its canvas
    Color8 graphCanvas;
    Color8 graphGrid;
    Color8 graphGridMajor;
    Color8 graphOrigin;                 // the "+" on the graph's 0, 0

    // ---- the graph: nodes
    Color8 nodeBody;
    Color8 nodeInset;                   // thumbnail well, value fields
    Color8 nodeRow;                     // layer rows
    Color8 nodeRowSelected;                 // a stack row picked: over its row ...
    Color8 nodeRowSelectedText;             // ... its name
    Color8 nodeRowOrigin;                   // a row a drag carries: its spot, outlined dashed
    Color8 nodeRowHot;
    Color8 nodeBorder;
    Color8 nodeBorderHot;
    Color8 activeRaster;                // the raster the canvas paints into
    Color8 nodeShadow;
    Color8 nodeSeparator;
    Color8 rasterHeader;                // each kind's title bar
    Color8 referenceHeader;
    Color8 moveHeader;
    Color8 compositorHeader;
    Color8 masterHeader;                // the master's, over its kind's
    Color8 nodeTitle;
    Color8 nodeText;
    Color8 nodeTextDim;
    Color8 iconOff;
    Color8 iconOn;
    Color8 emptyStack;                  // a stack with nothing in it: its frame
    Color8 liftShadow;                  // under a layer row being dragged
    Color8 chipEdge;                    // the blend mode chip
    Color8 channelA;                    // the channel letters
    Color8 channelR;
    Color8 channelG;
    Color8 channelB;

    // ---- the graph: ports and links
    Color8 imagePort;                   // image data
    Color8 maskPort;                    // greyscale
    Color8 portRing;
    Color8 linkLoose;                   // dragged, over nothing it can land on
    Color8 linkGlow;                    // under a hovered link
    Color8 fadingGlow;                  // ... a link into the master
    Color8 insertFill;                  // a link's insert button
    Color8 insertRim;
    Color8 insertPlus;

    // ---- the graph: its minimap
    Color8 mapBack;
    Color8 mapFrame;
    Color8 mapNode;
    Color8 mapEdge;
    Color8 mapMaskEdge;

    static const Palette& current();   // made from the theme in use
    static uint64_t revision();         // changes with the theme: what's made from the palette is made again
    static const std::vector<std::string_view>& themes();   // the built-in ones, by name
    // Uses a built-in theme (themes()); false (and why) if it can't be read: the one in use stays.
    static bool useTheme(std::string_view name, std::string& why);
};
