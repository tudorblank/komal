#include "ui/style/palette.hpp"
#include "ui/style/theme.hpp"

namespace {

Palette g_palette;
uint64_t g_revision = 0;

Palette make(const Theme& t)
{
    const auto& r = t.ramp;   // its steps, by number (the theme file says what each is for)
    Palette p;
    // panels: their surfaces and text (ImGui's theme is made of these too)
    p.text               = r[20];
    p.textDim            = r[16];
    p.panel              = r[4];
    p.bar                = r[2];
    p.popup              = r[5].withAlpha(250);
    p.border             = r[8].withAlpha(204);
    p.field              = r[6];
    p.fieldHot           = r[8];
    p.fieldActive        = r[9];
    p.button             = r[7];
    p.buttonHot          = r[9];
    p.buttonActive       = r[10];
    p.row                = r[7];
    p.rowHot             = r[9];
    p.rowActive          = r[10];
    p.rowAlt             = r[23].withAlpha(5);
    p.tableHeader        = r[6];
    p.tableBorder        = r[8];
    p.tableLine          = r[6];
    p.scrollGrab         = r[10];
    p.scrollGrabHot      = r[12];
    p.scrollGrabActive   = r[13];
    p.resizeGrip         = r[13].withAlpha(51);
    p.plot               = r[18];
    p.navHighlight       = r[23].withAlpha(179);
    p.navDim             = t.shadow.withAlpha(102);
    p.modalDim           = t.shadow.withAlpha(128);
    p.tabShown           = r[5];
    p.tabHot             = r[3];
    p.tabShownText       = r[22];
    p.introBack          = r[3];
    p.introFace          = r[8];
    p.none               = t.shadow.withAlpha(0);
    // the accent: selections, focus, checked things, the parts a value covers
    p.accent             = t.accent;
    p.accentDim          = t.accentDeep;
    // sliders and toggles
    p.track              = r[9];
    p.nodeTrack          = r[3];
    p.grab               = r[19];
    p.grabHot            = r[23];
    p.grabRim            = r[0];
    p.toggleEdge         = r[14];
    p.toggleHot          = r[17];
    p.checkMark          = r[3];
    // colour boxes and markers: readable on any colour
    p.darkRim            = t.shadow.withAlpha(200);
    p.lightRim           = Theme::kPureWhite.withAlpha(36);
    p.marker             = Theme::kPureWhite;
    p.white              = Theme::kPureWhite;
    p.black              = Theme::kPureBlack;
    p.clear              = Theme::kPureBlack.withAlpha(0);
    // behind everything
    p.windowBackground   = r[1];
    p.canvasBackground   = r[3];
    // pictures: behind their transparent parts
    p.canvasCheckerA     = r[3];
    p.canvasCheckerB     = r[6];
    p.pictureCheckerA    = r[9];
    p.pictureCheckerB    = r[12];
    // marks over a picture
    p.viewOutline        = t.red;
    p.emptyArea          = r[11];
    p.faintOutline       = r[23].withAlpha(90);
    p.originMark         = r[23].withAlpha(115);
    p.hudBack            = r[1].withAlpha(140);
    p.hudText            = r[20];
    // artboards on the canvas
    p.artboardFrame      = r[16];
    p.artboardLabel      = r[18];
    // notices: something went wrong
    p.noticeBack         = r[5].withAlpha(235);
    p.noticeMark         = t.red;
    // the graph: its canvas
    p.graphCanvas        = r[3];
    p.graphGrid          = r[23].withAlpha(7);
    p.graphGridMajor     = r[23].withAlpha(13);
    p.graphOrigin        = r[23].withAlpha(110);
    // the graph: nodes
    p.nodeBody           = r[7];
    p.nodeInset          = r[5];
    p.nodeRow            = r[9];
    p.nodeRowSelected    = t.accentDeep.withAlpha(80);
    p.nodeRowSelectedText = t.accent;
    p.nodeRowOrigin      = r[2];
    p.nodeRowHot         = r[10];
    p.nodeBorder         = r[0];
    p.nodeBorderHot      = r[15];
    p.activeRaster       = t.orange;
    p.nodeShadow         = t.shadow.withAlpha(60);
    p.nodeSeparator      = t.shadow.withAlpha(110);
    p.rasterHeader       = t.wine;
    p.referenceHeader    = t.ocean;
    p.moveHeader         = t.violet;
    p.compositorHeader   = t.teal;
    p.masterHeader       = t.maroon;
    p.nodeTitle          = r[21];
    p.nodeText           = r[20];
    p.nodeTextDim        = r[18];
    p.iconOff            = r[16];
    p.iconOn             = r[22];
    p.emptyStack         = r[23].withAlpha(28);
    p.liftShadow         = t.shadow.withAlpha(90);
    p.chipEdge           = r[23].withAlpha(50);
    p.channelA           = r[20];
    p.channelR           = t.rose;
    p.channelG           = t.green;
    p.channelB           = t.blue;
    // the graph: ports and links
    p.imagePort          = t.yellow;
    p.maskPort           = r[18];
    p.portRing           = r[0];
    p.linkLoose          = r[21].withAlpha(170);
    p.linkGlow           = r[23].withAlpha(70);
    p.fadingGlow         = r[23].withAlpha(50);
    p.insertFill         = r[8];
    p.insertRim          = r[20];
    p.insertPlus         = r[22];
    // the graph: its minimap
    p.mapBack            = r[1].withAlpha(210);
    p.mapFrame           = r[9];
    p.mapNode            = r[13];
    p.mapEdge            = r[14];
    p.mapMaskEdge        = r[12];
    return p;
}

}

const Palette& Palette::current() { return g_palette; }
uint64_t Palette::revision() { return g_revision; }
const std::vector<std::string_view>& Palette::themes() { return Theme::names(); }

bool Palette::useTheme(std::string_view name, std::string& why)
{
    const std::optional<Theme> theme = Theme::load(name, why);
    if (!theme) return false;
    g_palette = make(*theme);
    ++g_revision;
    return true;
}
