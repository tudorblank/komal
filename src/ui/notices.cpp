#include "ui/notices.hpp"
#include "base/frames.hpp"
#include "base/log.hpp"
#include "ui/render/painter.hpp"
#include "ui/style/ui-style.hpp"

#include <imgui.h>
#include <algorithm>

void Notices::post(std::string text)
{
    Log::info("notice", "%s", text.c_str());
    m_shown.push_back({ std::move(text), Deadline::in(kShow) });
    while (m_shown.size() > kMaxShown) m_shown.pop_front();
}

void Notices::draw()
{
    while (!m_shown.empty() && m_shown.front().until.expired()) m_shown.pop_front();
    if (m_shown.empty()) return;
    const Style::NoticeStyle& style = Style::get().notice;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const Painter draw = Painter::foreground();
    const float font = ImGui::GetFontSize();
    const float maxWidth = std::min(style.maxWidth, vp->WorkSize.x * 0.5f);
    float bottom = vp->WorkPos.y + vp->WorkSize.y - style.padding;
    for (auto it = m_shown.rbegin(); it != m_shown.rend(); ++it) {   // newest at the bottom
        const ImVec2 text = ImGui::CalcTextSize(it->text.c_str(), nullptr, false, maxWidth);
        const ImVec2 b(vp->WorkPos.x + vp->WorkSize.x - style.padding, bottom);
        const ImVec2 a(b.x - text.x - 2.0f * style.padding, b.y - text.y - 2.0f * style.padding);
        draw.fillRect(a, b, style.back, style.rounding);
        draw.fillRect(a, ImVec2(a.x + style.markWidth, b.y), style.mark, style.rounding, ImDrawFlags_RoundCornersLeft);
        draw.text(ImGui::GetFont(), font, ImVec2(a.x + style.padding, a.y + style.padding), style.text, it->text.c_str(), nullptr, maxWidth);
        bottom = a.y - style.gap;
    }
}

Clock::Seconds Notices::nextFrameIn() const
{
    if (m_shown.empty()) return kNoFrame;
    return m_shown.front().until.left();
}
