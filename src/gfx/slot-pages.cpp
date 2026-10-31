#include "gfx/slot-pages.hpp"

#include <algorithm>
#include <cassert>

void SlotPages::configure(const Config& config)
{
    assert(m_pages.empty() && "configure before the first allocate()");
    m_config = config;
}

bool SlotPages::makeTexture(WGPUDevice device, Page& page) const
{
    const uint32_t size = uint32_t(m_config.pageSize);
    return page.texture.create(device, { size, size, m_config.format, m_config.usage, m_config.label });
}

SlotPages::Slot SlotPages::allocate(WGPUDevice device)
{
    // A page with a texture and room first; then one that gave its texture back; then a new one.
    const auto room = [](const Page& p) { return !p.freeSlots.empty(); };
    auto page = std::find_if(m_pages.begin(), m_pages.end(), [&](const Page& p) { return room(p) && p.texture; });
    if (page == m_pages.end()) {
        page = std::find_if(m_pages.begin(), m_pages.end(), room);
        if (page != m_pages.end() && !makeTexture(device, *page)) return {};
    }
    if (page == m_pages.end()) {
        Page p;
        if (!makeTexture(device, p)) return {};
        const int count = slotsPerPage();
        p.freeSlots.resize(size_t(count));
        for (int i = 0; i < count; ++i) p.freeSlots[size_t(i)] = uint16_t(count - 1 - i);
        m_pages.push_back(std::move(p));
        page = m_pages.end() - 1;
    }
    page->emptySince = {};
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
    const Clock::TimePoint now = Clock::now();
    const size_t all = size_t(slotsPerPage());
    for (Page& page : m_pages) {
        page.freeSlots.insert(page.freeSlots.end(), page.freed.begin(), page.freed.end());
        page.freed.clear();
        if (page.freeSlots.size() < all || !page.texture) {
            page.emptySince = {};
            continue;
        }
        if (page.emptySince == Clock::TimePoint{}) page.emptySince = now;
        else if (Clock::between(page.emptySince, now) >= m_config.keepEmpty) page.texture.reset();   // nothing drew it since it emptied
    }
}

size_t SlotPages::bytes() const
{
    size_t total = 0;
    for (const Page& page : m_pages)
        if (page.texture) total += size_t(page.texture.width()) * page.texture.height() * 4;   // every page format is 4 bytes a texel
    return total;
}

void SlotPages::release()
{
    m_pages.clear();
}
