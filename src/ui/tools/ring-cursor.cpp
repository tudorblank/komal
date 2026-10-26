#include "ring-cursor.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace {

// Straight-alpha ARGB, as SDL color cursors take it: `src` drawn over `dst`.
Uint32 blendOver(Uint32 dst, float r, float g, float b, float a)
{
    const float da = float(dst >> 24) / 255.0f;
    const float oa = a + da * (1.0f - a);
    if (oa <= 0.0f) return 0;
    const auto ch = [&](int shift, float s) {
        const float d = float((dst >> shift) & 0xFF) / 255.0f;
        return Uint32(std::clamp((s * a + d * da * (1.0f - a)) / oa, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return (Uint32(oa * 255.0f + 0.5f) << 24) | (ch(16, r) << 16) | (ch(8, g) << 8) | ch(0, b);
}

SDL_Cursor* makeRing(int diameter, bool erase)
{
    const float radius = diameter * 0.5f;
    const int   arm    = 5;                                   // crosshair arm, pixels
    const int   size   = std::max(diameter + 6, 2 * arm + 5) | 1;   // odd: the hot spot is a pixel
    const int   c      = size / 2;
    SDL_Surface* s = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return nullptr;
    SDL_FillSurfaceRect(s, nullptr, 0);
    const float light[3] = { 1.0f, erase ? 0.55f : 1.0f, erase ? 0.55f : 1.0f };   // erasing: pink

    for (int y = 0; y < size; ++y) {
        Uint32* row = reinterpret_cast<Uint32*>(static_cast<Uint8*>(s->pixels) + y * s->pitch);
        for (int x = 0; x < size; ++x) {
            Uint32 p = 0;
            const float dx = float(x - c), dy = float(y - c);
            // The ring: dark outside a light line, so it reads on any content.
            if (diameter > 0) {
                const float off = std::fabs(std::sqrt(dx * dx + dy * dy) - radius);
                const float dark  = std::clamp(1.75f - off, 0.0f, 1.0f);
                const float lit   = std::clamp(0.85f - off, 0.0f, 1.0f);
                if (dark > 0.0f) p = blendOver(p, 0.0f, 0.0f, 0.0f, dark * 0.65f);
                if (lit > 0.0f)  p = blendOver(p, light[0], light[1], light[2], lit * 0.9f);
            }
            // The crosshair on the hot spot.
            const int ax = std::abs(x - c), ay = std::abs(y - c);
            const bool onArm  = (ay == 0 && ax <= arm) || (ax == 0 && ay <= arm);
            const bool nearArm = (ay <= 1 && ax <= arm + 1) || (ax <= 1 && ay <= arm + 1);
            if (nearArm) p = blendOver(p, 0.0f, 0.0f, 0.0f, 0.6f);
            if (onArm)   p = blendOver(p, 1.0f, 1.0f, 1.0f, 0.9f);
            row[x] = p;
        }
    }
    SDL_Cursor* cursor = SDL_CreateColorCursor(s, c, c);
    SDL_DestroySurface(s);
    return cursor;
}

} // namespace

SDL_Cursor* RingCursors::get(float radius, bool erase)
{
    const int diameter = fits(radius) && radius >= 2.0f ? int(std::lround(radius * 2.0f)) : 0;
    ++m_clock;
    for (Entry& e : m_entries)
        if (e.diameter == diameter && e.erase == erase) { e.used = m_clock; return e.cursor; }

    SDL_Cursor* cursor = makeRing(diameter, erase);
    if (!cursor) return nullptr;
    if (m_entries.size() >= kKeep) {   // the least recently used goes (never the one in use: it was just used)
        auto old = std::min_element(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) { return a.used < b.used; });
        SDL_DestroyCursor(old->cursor);
        m_entries.erase(old);
    }
    m_entries.push_back({ diameter, erase, cursor, m_clock });
    return cursor;
}

void RingCursors::clear()
{
    for (Entry& e : m_entries) SDL_DestroyCursor(e.cursor);
    m_entries.clear();
}
