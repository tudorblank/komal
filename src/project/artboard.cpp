#include "project/artboard.hpp"
#include "project/fits.hpp"

#include <algorithm>

const Artboard* ArtboardList::find(ArtboardId id) const
{
    for (const Artboard& a : m_list)
        if (a.id == id) return &a;
    return nullptr;
}

ArtboardId ArtboardList::add(const BoundsI& rect)
{
    if (!rectFits(rect)) return kNoArtboard;
    m_last = ArtboardId(uint32_t(m_last) + 1);
    Artboard a;
    a.id   = m_last;
    a.rect = rect;
    a.name = "Artboard " + std::to_string(uint32_t(a.id));   // ids never come back: nor do names
    m_list.push_back(a);
    return a.id;
}

bool ArtboardList::set(const Artboard& a)
{
    const auto now = std::find_if(m_list.begin(), m_list.end(), [&](const Artboard& b) { return b.id == a.id; });
    if (now == m_list.end() || *now == a || !fits(a)) return false;
    *now = a;
    return true;
}

bool ArtboardList::remove(ArtboardId id)
{
    return std::erase_if(m_list, [&](const Artboard& a) { return a.id == id; }) > 0;
}

void ArtboardList::open(const Artboards& list, ArtboardId last)
{
    m_list = list;
    m_last = last;
}

bool ArtboardList::match(const std::vector<StackGroup>& groups) const
{
    if (groups.size() != m_list.size()) return false;
    return std::all_of(m_list.begin(), m_list.end(), [&](const Artboard& a) {
        return std::count_if(groups.begin(), groups.end(), [&](const StackGroup& g) { return g.key == a.id; }) == 1;
    });
}

std::vector<GroupLook> ArtboardList::looks() const
{
    std::vector<GroupLook> out;
    for (const Artboard& a : m_list) out.push_back({ a.id, a.rect, a.color, !a.visible });
    return out;
}
