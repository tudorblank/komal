#include "ui/input-owner.hpp"

#include <imgui.h>

void InputOwner::beginFrame()
{
    m_typing   = ImGui::GetIO().WantTextInput;
    m_keyboard = m_hasClaim ? m_claimed : ActionScope::Global;
    m_hasClaim = false;   // claimed afresh while this frame draws
}

void InputOwner::claim(ActionScope panel, Claim how)
{
    if (m_hasClaim && how <= m_how) return;
    m_claimed  = panel;
    m_how      = how;
    m_hasClaim = true;
}
