#pragma once
// Brush-outline cursors made at runtime and handed to the system (SystemCursors::setCustom), so
// the outline follows the mouse with no frame of latency, like the system arrow does. A ring
// the size of the brush on screen, with a small crosshair on the hot spot.
//
// The system can only show cursors up to a point (kMaxRadius); past it, get() makes a plain
// crosshair and the ring is left to the tool's overlay.

#include <cstdint>
#include <vector>

struct SDL_Cursor;

class RingCursors {
public:
    static constexpr float kMaxRadius = 124.0f;   // points: a 256 px cursor
    static bool fits(float radius) { return radius <= kMaxRadius; }

    ~RingCursors() { clear(); }
    // A ring of `radius` screen points (0 or too big: just the crosshair). Kept until clear(), or
    // until enough other sizes pushed it out.
    SDL_Cursor* get(float radius, bool erase);
    void clear();

private:
    struct Entry { int diameter; bool erase; SDL_Cursor* cursor; uint64_t used; };
    static constexpr size_t kKeep = 24;   // sizes cached (a pen's pressure sweeps through many)

    std::vector<Entry> m_entries;
    uint64_t m_clock = 0;
};
