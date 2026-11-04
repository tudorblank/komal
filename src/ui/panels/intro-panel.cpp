#include "intro-panel.hpp"
#include "base/log.hpp"
#include "command/actions.hpp"
#include "document/image-load.hpp"
#include "gpu/device.hpp"
#include "ui/panels/canvas/imgui-texture.hpp"
#include "ui/render/painter.hpp"
#include "ui/style/ui-style.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <vector>

extern const unsigned char kKomalFace[];   // generated, see embed_file() in CMakeLists.txt
extern const unsigned int  kKomalFaceSize;

namespace {

// Each pixel of the w x h mask the average of the source's alpha under it (a box at least a pixel
// wide); written white, so a tint is its colour.
std::vector<uint8_t> averagedMask(const DecodedImage& src, int w, int h)
{
    const FlatImage f = src.flat();
    const double sx = double(f.width) / w, sy = double(f.height) / h;
    std::vector<uint8_t> out(size_t(w) * size_t(h) * 4, 255);
    for (int y = 0; y < h; ++y) {
        const int y0 = int(y * sy), y1 = std::max(y0 + 1, std::min(f.height, int(std::ceil((y + 1) * sy))));
        for (int x = 0; x < w; ++x) {
            const int x0 = int(x * sx), x1 = std::max(x0 + 1, std::min(f.width, int(std::ceil((x + 1) * sx))));
            uint64_t sum = 0;
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx) sum += f.rgba[(size_t(yy) * size_t(f.width) + size_t(xx)) * 4 + 3];
            out[(size_t(y) * size_t(w) + size_t(x)) * 4 + 3] = uint8_t((sum + uint64_t((y1 - y0) * (x1 - x0)) / 2) / uint64_t((y1 - y0) * (x1 - x0)));
        }
    }
    return out;
}

}

bool IntroPanel::makeFace(int width)
{
    DecodedImage src;
    std::string why;
    if (!loadImageBytes(std::span<const uint8_t>(kKomalFace, kKomalFaceSize), src, why)) {
        Log::error("intro", "the face: %s", why.c_str());
        return false;
    }
    m_aspect = float(src.height()) / float(src.width());
    const int w = std::clamp(width, 1, src.width()), h = std::max(1, int(std::lround(float(w) * m_aspect)));
    const std::vector<uint8_t> mask = averagedMask(src, w, h);
    if (!m_face.create(m_gpu->device(), { uint32_t(w), uint32_t(h), WGPUTextureFormat_RGBA8Unorm,
                                          WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst, "Intro face" }))
        return false;
    WGPUTexelCopyTextureInfo dst{};
    dst.texture = m_face.texture();
    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow  = uint32_t(w) * 4;
    layout.rowsPerImage = uint32_t(h);
    const WGPUExtent3D size = { uint32_t(w), uint32_t(h), 1 };
    wgpuQueueWriteTexture(m_gpu->queue(), &dst, mask.data(), mask.size(), &layout, &size);
    m_faceWidth = width;
    return true;
}

void IntroPanel::draw(ActionRegistry& actions)
{
    const Style::IntroStyle& style = Style::get().intro;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, style.back);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    const bool shown = ImGui::Begin("##intro", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                                        ImGuiWindowFlags_NoNav);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!shown) {
        ImGui::End();
        return;
    }

    // The face at whole pixels of the screen: made again when the UI's scale changes.
    const int pixels = int(std::lround(style.face * ImGui::GetIO().DisplayFramebufferScale.x));
    if (!m_failed && m_gpu && pixels != m_faceWidth) m_failed = !makeFace(pixels);
    const bool face = m_face && !m_failed;
    const float faceH = face ? std::round(style.face * m_aspect) : 0.0f;
    const float rowH  = Style::metrics().rowHeight;
    const float total = faceH + (face ? style.gap : 0.0f) + rowH;

    // The whole a little above the middle.
    const ImVec2 at = vp->WorkPos, size = vp->WorkSize;
    const float top  = std::round(at.y + (size.y - total) * (0.5f - style.lift));
    const float midX = at.x + size.x * 0.5f;
    if (face) {
        const ImVec2 a(std::round(midX - style.face * 0.5f), top);
        Painter::window().image(imguiTexture(m_face.view()), a, ImVec2(a.x + style.face, a.y + faceH), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), style.tint);
    }
    const float rowW = 2.0f * style.buttonWidth + style.gap;
    ImGui::SetCursorScreenPos(ImVec2(std::round(midX - rowW * 0.5f), top + total - rowH));
    if (Widgets::button("New", style.buttonWidth)) actions.run(Actions::DocNew);
    ImGui::SameLine(0.0f, style.gap);
    if (Widgets::button("Open", style.buttonWidth)) actions.run(Actions::FileOpen);
    ImGui::End();
}
