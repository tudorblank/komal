#include "app/input.hpp"

void Input::beginFrame()
{
    m_collector.beginFrame();
    m_imgui.beginFrame();
}

void Input::handle(const SDL_Event& e)
{
    m_collector.handle(e);
    m_imgui.push(e);
}

void Input::endEvents()
{
    m_imgui.endEvents();
}
