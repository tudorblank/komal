#include "tool-manager.h"
#include "ui/tools/brush-tool.h"
#include "ui/tools/hand-tool.h"

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

void ToolManager::select(Tool& tool, Project& project)
{
    if (&tool == m_selected) return;
    m_selected->cancel(project);
    m_selected = &tool;
}

void ToolManager::handleShortcuts(Project& project, const InputFrame& input, bool typing)
{
    if (typing || active().busy()) return;   // never switch under a stroke or a drag
    for (const auto& t : m_tools)
        if (input.pressed({ t->shortcut() })) select(*t, project);
    if (input.pressed({ SDLK_E })) {
        select(*m_brush, project);
        m_brush->setErase(true);
    } else if (input.pressed({ SDLK_B })) {
        m_brush->setErase(false);
    }
}

void ToolManager::setSpringHand(bool held)
{
    if (!held && m_springHand && m_hand->busy()) return;        // finish the drag first
    if (held && !m_springHand && m_selected->busy()) return;    // and never cut into a stroke
    m_springHand = held;
}
