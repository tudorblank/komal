#pragma once

#include <SDL3/SDL_events.h>

#include <cstdint>
#include <vector>

enum class PointerDevice : uint8_t {
    Mouse,
    Pen,      // a pen's drawing tip
    Eraser,   // a pen's eraser end
};

// Buttons, the same for every device.
enum class PointerButton : uint8_t {
    Primary   = 0,   // left mouse button, pen tip touching
    Secondary = 1,   // right mouse button, pen barrel button 1
    Middle    = 2,   // middle mouse button, pen barrel button 2
};

struct PointerEvent {
    enum class Type : uint8_t { Move, Down, Up };
    Type          type     = Type::Move;
    PointerDevice device   = PointerDevice::Mouse;
    PointerButton button   = PointerButton::Primary;   // Down / Up
    float         x = 0.0f, y = 0.0f;                  // window coordinates (points, like ImGui's)
    float         pressure = 1.0f;                     // 0..1; a mouse is always 1
    float         tiltX = 0.0f, tiltY = 0.0f;          // degrees, -90..90; 0 for a mouse
    uint64_t      timeNs   = 0;                        // SDL timestamp
};

// Modifier keys, left and right alike.
enum KeyMod : uint8_t {
    ModNone  = 0,
    ModCtrl  = 1 << 0,
    ModShift = 1 << 1,
    ModAlt   = 1 << 2,
    ModSuper = 1 << 3,   // Cmd on macOS, the Windows key elsewhere
#if defined(__APPLE__)
    ModPrimary = ModSuper,   // the platform's command modifier: Cmd+Z on macOS...
#else
    ModPrimary = ModCtrl,    // ... Ctrl+Z elsewhere
#endif
};

struct KeyEvent {
    SDL_Keycode  key      = SDLK_UNKNOWN;        // by the keyboard layout (what the key types)
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN; // by position on the keyboard
    uint8_t      mods     = ModNone;             // KeyMod flags held with it
    bool         down     = true;
    bool         repeat   = false;               // the OS's key repeat, while held
    uint64_t     timeNs   = 0;
};

// A key with an exact set of modifiers: Shortcut{ SDLK_Z, ModPrimary | ModShift }.
struct Shortcut {
    SDL_Keycode key;
    uint8_t     mods = ModNone;
};

// one frame worth of input
struct InputFrame {
    std::vector<PointerEvent> pointer;   // in the order they happened

    std::vector<KeyEvent> keys;          // in the order they happened
    uint8_t mods = ModNone;              // modifiers held now
    std::vector<SDL_Keycode> held;       // keys held now

    // Went down this frame with exactly these modifiers (repeats count only if asked).
    bool pressed(Shortcut s, bool repeats = false) const;
    bool isDown(SDL_Keycode key) const;

    // The pen as it was last reported (not reset between frames).
    struct Pen {
        bool  present  = false;   // a pen has been used since the app started
        bool  touching = false;
        bool  eraser   = false;
        float pressure = 0.0f;
        float tiltX = 0.0f, tiltY = 0.0f;
    } pen;
    PointerDevice lastDevice = PointerDevice::Mouse;
};

// Turns SDL events into InputFrames. Feed it every event, in order, as it arrives.
class InputCollector {
public:
    void beginFrame();                  // before the frame's events
    void handle(const SDL_Event& e);
    const InputFrame& frame() const { return m_frame; }

private:
    void push(PointerEvent ev);
    void releaseAllKeys();
    PointerEvent penEvent(PointerEvent::Type type, float x, float y, uint64_t timeNs) const;

    InputFrame m_frame;
};