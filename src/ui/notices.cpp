#include "ui/notices.hpp"
#include "base/frames.hpp"
#include "base/log.hpp"
#include "base/palette.hpp"

#include <imgui.h>
#include <algorithm>

void Notices::post(std::string text)
{
    Log::info("notice", "%s", text.c_str());
    m_shown.push_back({ std::move(text), Deadline::in(kShowMs) });
    while (m_shown.size() > kMaxShown) m_shown.pop_front();
}

void Notices::draw()
{
    while (!m_shown.empty() && m_shown.front().until.expired()) m_shown.pop_front();
    if (m_shown.empty()) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList(vp);
    const float font = ImGui::GetFontSize(), pad = font * 0.6f, gap = font * 0.4f;
    const float maxWidth = std::min(font * 28.0f, vp->WorkSize.x * 0.5f);
    float bottom = vp->WorkPos.y + vp->WorkSize.y - pad;
    for (auto it = m_shown.rbegin(); it != m_shown.rend(); ++it) {   // newest at the bottom
        const ImVec2 text = ImGui::CalcTextSize(it->text.c_str(), nullptr, false, maxWidth);
        const ImVec2 b(vp->WorkPos.x + vp->WorkSize.x - pad, bottom);
        const ImVec2 a(b.x - text.x - 2.0f * pad, b.y - text.y - 2.0f * pad);
        dl->AddRectFilled(a, b, Palette::kNoticeBack.packed(), font * 0.3f);
        dl->AddRectFilled(a, ImVec2(a.x + font * 0.2f, b.y), Palette::kNoticeMark.packed(), font * 0.3f, ImDrawFlags_RoundCornersLeft);
        dl->AddText(ImGui::GetFont(), font, ImVec2(a.x + pad, a.y + pad), Palette::kHudText.packed(), it->text.c_str(), nullptr, maxWidth);
        bottom = a.y - gap;
    }
}

double Notices::nextFrameIn() const
{
    if (m_shown.empty()) return kNoFrame;
    return m_shown.front().until.msLeft() / 1000.0;
}
