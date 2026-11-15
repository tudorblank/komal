#include "ui/widgets/hsv.hpp"
#include "base/color.hpp"

namespace Hsv {

ImU32 packed(float h, float s, float v, float a)
{
    float rgba[4] = { 0.0f, 0.0f, 0.0f, a };
    ImGui::ColorConvertHSVtoRGB(h, s, v, rgba[0], rgba[1], rgba[2]);
    return Color8::fromUnit(rgba).packed();
}

void hueStops(ImU32 (&stops)[kHueStops])
{
    for (int i = 0; i < kHueStops; ++i) stops[i] = packed(float(i) / float(kHueStops - 1), 1.0f, 1.0f);
}

void follow(const float rgb[3], float& h, float& s, float& v)
{
    float nh, ns, nv;
    ImGui::ColorConvertRGBtoHSV(rgb[0], rgb[1], rgb[2], nh, ns, nv);
    if (nv > 0.0f && ns > 0.0f) h = nh;
    if (nv > 0.0f) s = ns;
    v = nv;
}

}
