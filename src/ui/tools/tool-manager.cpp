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

void ToolManager::select(Tool& tool, Project& project)
{
    if (&tool == m_selected) return;
    m_selected->cancel(project);
    m_selected = &tool;
}

void ToolManager::registerActions(ActionRegistry& actions, Project& project)
{
    const auto idle = [this] { return !active().busy(); };   // never switch under a stroke or a drag
    for (const auto& t : m_tools) {
        Tool* tool = t.get();
        actions.add({ .id = toolActionId(*tool), .title = tool->name(), .shortcuts = { { tool->shortcut() } }, .enabled = idle,
                      .checked = [this, tool] { return m_selected == tool; },
                      .run = [this, tool, &project] {
                          select(*tool, project);
                          if (tool == m_brush) m_brush->setErase(false);
                      } });
        tool->registerActions(actions, [this, tool] { return &active() == tool; });
    }
    actions.add({ .id = "tool.eraser", .title = "Eraser", .shortcuts = { { SDLK_E } }, .enabled = idle,
                  .run = [this, &project] {
                      select(*m_brush, project);
                      m_brush->setErase(true);
                  } });
    actions.add({ .id = "color.swap", .title = "Swap Colors", .shortcuts = { { SDLK_X } },
                  .run = [this] { m_colors.swap(); } });
}

void ToolManager::setSpringHand(bool held)
{
    if (!held && m_springHand && m_hand->busy()) return;        // finish the drag first
    if (held && !m_springHand && m_selected->busy()) return;    // and never cut into a stroke
    m_springHand = held;
}
