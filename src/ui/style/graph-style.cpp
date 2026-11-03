#include "ui/style/graph-style.hpp"
#include "node/node.hpp"
#include "ui/style/palette.hpp"

namespace GraphStyle {

namespace {

Colors make(const Palette& p)
{
    Colors c;
    // the canvas
    c.canvas       = p.graphCanvas.packed();
    c.gridLine     = p.graphGrid.packed();
    c.gridMajor    = p.graphGridMajor.packed();
    c.origin       = p.graphOrigin.packed();
    c.rubberFill   = p.accent.withAlpha(32).packed();
    c.rubberEdge   = p.accent.withAlpha(190).packed();
    // nodes
    c.body         = p.nodeBody.packed();
    c.inset        = p.nodeInset.packed();
    c.row          = p.nodeRow.packed();
    c.rowHovered   = p.nodeRowHot.packed();
    c.border       = p.nodeBorder.packed();
    c.borderHover  = p.nodeBorderHot.packed();
    c.selection    = p.accent.packed();
    c.activeRaster = p.activeRaster.packed();
    c.shadow       = p.nodeShadow.packed();
    c.separator    = p.nodeSeparator.packed();
    c.rasterHeader     = p.rasterHeader.packed();
    c.referenceHeader  = p.referenceHeader.packed();
    c.moveHeader       = p.moveHeader.packed();
    c.compositorHeader = p.compositorHeader.packed();
    c.headerOutput = p.masterHeader.packed();
    c.title        = p.nodeTitle.packed();
    c.text         = p.nodeText.packed();
    c.dimText      = p.nodeTextDim.packed();
    c.iconOff      = p.iconOff.packed();
    c.iconOn       = p.iconOn.packed();
    c.accent       = p.accent.packed();
    c.channel[0] = p.channelA.packed();
    c.channel[1] = p.channelR.packed();
    c.channel[2] = p.channelG.packed();
    c.channel[3] = p.channelB.packed();
    // layer stacks
    c.gapFill      = p.accent.withAlpha(36).packed();
    c.emptyStack   = p.emptyStack.packed();
    c.liftShadow   = p.liftShadow.packed();
    c.chipEdge     = p.chipEdge.packed();
    // ports and links
    c.imagePort    = p.imagePort.packed();
    c.maskPort     = p.maskPort.packed();
    c.portRing     = p.portRing.packed();
    c.imageLink    = p.imagePort.withAlpha(220).packed();
    c.maskLink     = p.maskPort.withAlpha(220).packed();
    c.linkLoose    = p.linkLoose.packed();
    c.linkGlow     = p.linkGlow.packed();
    c.fadingGlow   = p.fadingGlow.packed();
    c.insertFill   = p.insertFill.packed();
    c.insertRim    = p.insertRim.packed();
    c.insertPlus   = p.insertPlus.packed();
    // the minimap
    c.mapBack      = p.mapBack.packed();
    c.mapFrame     = p.mapFrame.packed();
    c.mapNode      = p.mapNode.packed();
    c.mapEdge      = p.mapEdge.packed();
    c.mapMaskEdge  = p.mapMaskEdge.packed();
    return c;
}

}

const Colors& colors()
{
    static Colors c;
    static uint64_t madeFor = 0;   // the palette's revision
    if (madeFor != Palette::revision()) {
        c = make(Palette::current());
        madeFor = Palette::revision();
    }
    return c;
}

ImU32 kindHeader(NodeType type)
{
    const Colors& c = colors();
    switch (type) {
    case NodeType::Raster:     return c.rasterHeader;
    case NodeType::Reference:  return c.referenceHeader;
    case NodeType::Move:       return c.moveHeader;
    case NodeType::Compositor: return c.compositorHeader;
    }
    return c.compositorHeader;
}

}
