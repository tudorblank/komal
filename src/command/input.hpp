#pragma once
// Input reaches the app two ways, from the same platform events (app/: Application, InputCollector):
//
//   ImGui (Application)      widgets, windows, docking, text; and who has the pointer: a panel
//                            owns it while ImGui says its item is hovered or held
//   InputFrame (here)        every pointer sample with its pen data, for the tools of the panel
//                            that owns the pointer; buttons held; key presses for shortcuts
//                            (ActionRegistry, dispatched once a frame); held keys (Space for the hand)
//
// Widgets read ImGui only; tools read the InputFrame only, and only while their panel owns the
// pointer. Nothing here knows the platform: the app turns its events into these.

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
    uint64_t      timeNs   = 0;                        // the platform's timestamp
};

// A key, by what it types in the keyboard's layout (not by where it sits). The keys the app can
// bind or read; any other is Unknown.
enum class Key : uint8_t {
    Unknown,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space, Enter, Escape, Tab, Backspace, Delete, Insert, Home, End, PageUp, PageDown,
    Left, Right, Up, Down,
    LeftBracket, RightBracket, Minus, Equals, Comma, Period, Slash, Backslash, Semicolon, Apostrophe, Grave,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    LeftCtrl, RightCtrl, LeftShift, RightShift, LeftAlt, RightAlt, LeftSuper, RightSuper,
    Count
};
const char* keyName(Key key);   // as help text shows it: "Z", "Delete", "["
bool isModifierKey(Key key);    // Ctrl, Shift, Alt, Super (either side)

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
    Key      key    = Key::Unknown;
    uint8_t  mods   = ModNone;   // KeyMod flags held with it
    bool     down   = true;
    bool     repeat = false;     // the OS's key repeat, while held
    uint64_t timeNs = 0;
};

// A key with an exact set of modifiers: Shortcut{ Key::Z, ModPrimary | ModShift }.
struct Shortcut {
    Key     key  = Key::Unknown;
    uint8_t mods = ModNone;
    bool operator==(const Shortcut&) const = default;
};

// one frame worth of input
struct InputFrame {
    std::vector<PointerEvent> pointer;   // in the order they happened

    std::vector<KeyEvent> keys;          // in the order they happened
    uint8_t mods = ModNone;              // modifiers held now
    std::vector<Key> held;               // keys held now

    // Pointer buttons held now, a bit per PointerButton (1 << button). The pen's tip is its Primary.
    uint8_t mouseButtons = 0;
    uint8_t penButtons   = 0;
    bool anyButtonHeld() const { return mouseButtons != 0 || penButtons != 0; }
    bool buttonHeld(PointerDevice device, PointerButton b) const
    {
        return ((unsigned(device == PointerDevice::Mouse ? mouseButtons : penButtons) >> unsigned(b)) & 1u) != 0;
    }

    // Went down this frame with exactly these modifiers (repeats count only if asked).
    bool pressed(Shortcut s, bool repeats = false) const;
    bool isDown(Key key) const;

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
