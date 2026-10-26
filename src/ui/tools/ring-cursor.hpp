#pragma once
// Brush-outline cursors made at runtime for the system (SystemCursors::setCustom): no frame of
// lag. A ring the brush's size with a crosshair; past kMaxRadius just the crosshair (the tool's
// overlay draws the ring).

#include <cstdint>
#include <vector>

struct SDL_Cursor;

class RingCursors {
public:
    static constexpr float kMaxRadius = 124.0f;   // points: a 256 px cursor
    static bool fits(float radius) { return radius <= kMaxRadius; }

    ~RingCursors() { clear(); }
    // A ring of `radius` screen points (0 or too big: just the crosshair). Cached, least recently used out.
    SDL_Cursor* get(float radius, bool erase);
    void clear();

private:
    struct Entry { int diameter; bool erase; SDL_Cursor* cursor; uint64_t used; };
    static constexpr size_t kKeep = 24;   // sizes cached (a pen's pressure sweeps through many)

    std::vector<Entry> m_entries;
    uint64_t m_clock = 0;
};
