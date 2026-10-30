#include "gfx/slot-pages.hpp"

#include <algorithm>
#include <cassert>

void SlotPages::configure(const Config& config)
{
    assert(m_pages.empty() && "configure before the first allocate()");
    m_config = config;
}

SlotPages::Slot SlotPages::allocate(WGPUDevice device)
{
    auto page = std::find_if(m_pages.begin(), m_pages.end(), [](const Page& p) { return !p.freeSlots.empty(); });
    if (page == m_pages.end()) {
        Page p;
        const uint32_t size = uint32_t(m_config.pageSize);
        if (!p.texture.create(device, { size, size, m_config.format, m_config.usage, m_config.label })) return {};
        const int count = slotsPerPage();
        p.freeSlots.resize(size_t(count));
        for (int i = 0; i < count; ++i) p.freeSlots[size_t(i)] = uint16_t(count - 1 - i);
        m_pages.push_back(std::move(p));
        page = m_pages.end() - 1;
    }
    const Slot s{ int(page - m_pages.begin()), page->freeSlots.back() };
    page->freeSlots.pop_back();
    return s;
}

void SlotPages::free(Slot s)
{
    if (s.page >= 0 && size_t(s.page) < m_pages.size()) m_pages[size_t(s.page)].freed.push_back(uint16_t(s.slot));
}

void SlotPages::freeNow(Slot s)
{
    if (s.page >= 0 && size_t(s.page) < m_pages.size()) m_pages[size_t(s.page)].freeSlots.push_back(uint16_t(s.slot));
}

void SlotPages::recycle()
{
    for (Page& page : m_pages) {
        page.freeSlots.insert(page.freeSlots.end(), page.freed.begin(), page.freed.end());
        page.freed.clear();
    }
}

void SlotPages::release()
{
    m_pages.clear();
}
