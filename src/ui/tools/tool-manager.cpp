#include "tool-manager.hpp"
#include "ui/tools/brush-tool.hpp"
#include "ui/tools/hand-tool.hpp"

ToolManager::ToolManager()
{
    auto brush = std::make_unique<BrushTool>(m_colors);
    auto hand  = std::make_unique<HandTool>();
    m_brush = brush.get();
    m_hand  = hand.get();
    m_tools.push_back(std::move(brush));
    m_tools.push_back(std::move(hand));
    m_selected = m_brush;
}

ToolManager::~ToolManager() = default;

void ToolManager::select(Tool& tool, ActionRegistry& actions)
{
    if (&tool == m_selected) return;
    m_selected->cancel(actions);
    m_selected = &tool;
}

void ToolManager::setSpringHand(bool held)
{
    if (!held && m_springHand && m_hand->busy()) return;        // finish the drag first
    if (held && !m_springHand && m_selected->busy()) return;    // and never cut into a stroke
    m_springHand = held;
}
