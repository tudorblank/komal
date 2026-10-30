#include "tool-manager.hpp"
#include "ui/tools/brush-tool.hpp"
#include "ui/tools/hand-tool.hpp"

ToolManager::ToolManager()
{
    auto brush = std::make_unique<BrushTool>(m_colors);
    auto hand  = std::make_unique<HandTool>();
    m_selected = brush.get();
    m_hand     = hand.get();
    m_tools.push_back(std::move(brush));
    m_tools.push_back(std::move(hand));
}

ToolManager::~ToolManager() = default;

void ToolManager::registerActions(ActionRegistry& actions)
{
    using K = ActionKind;

    // Each tool's pick, on its own key (no modifiers), never under a stroke or a drag.
    const auto idle = [this] { return !active().busy(); };
    for (const auto& t : m_tools) {
        Tool* tool = t.get();
        actions.add({ .id = tool->pickAction(), .title = tool->name(), .kind = K::AppState, .shortcuts = { { tool->shortcut() } },
                      .enabled = idle, .checked = [this, tool] { return m_selected == tool; },
                      .run = [this, tool, &actions] {
                          select(*tool, actions);
                          tool->picked();
                      } });
    }

    // The colours: target, then the channels it changes (straight, 0..1).
    const ArgSpec channel[] = { Arg::number("r", 0.0, 1.0), Arg::number("g", 0.0, 1.0), Arg::number("b", 0.0, 1.0), Arg::number("a", 0.0, 1.0) };
    actions.add({ .id = Actions::ColorSet, .title = "Set Color", .kind = K::AppState,
                  .args = { Arg::choice("target", { "primary", "secondary" }), channel[0].optional(), channel[1].optional(),
                            channel[2].optional(), channel[3].optional() },
                  .coalesce = true, .run = [this](const ActionArgs& a) {
        float* c = a.text("target") == "secondary" ? m_colors.background : m_colors.foreground;
        c[0] = float(a.number("r", c[0]));
        c[1] = float(a.number("g", c[1]));
        c[2] = float(a.number("b", c[2]));
        c[3] = float(a.number("a", c[3]));
    } });
    actions.add({ .id = Actions::ColorSwap, .title = "Swap Colors", .kind = K::AppState, .shortcuts = { { SDLK_X } },
                  .run = [this] { m_colors.swap(); } });

    for (const auto& t : m_tools) t->registerActions(actions, *this);
}

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
