#include "command/input.hpp"

#include <algorithm>
#include <iterator>

namespace {

    constexpr const char* kKeyNames[] = {
        "Unknown",
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        "Space", "Enter", "Escape", "Tab", "Backspace", "Delete", "Insert", "Home", "End", "PageUp", "PageDown",
        "Left", "Right", "Up", "Down",
        "[", "]", "-", "=", ",", ".", "/", "\\", ";", "'", "`",
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
        "Left Ctrl", "Right Ctrl", "Left Shift", "Right Shift", "Left Alt", "Right Alt", "Left Super", "Right Super",
    };
    static_assert(std::size(kKeyNames) == size_t(Key::Count), "a name per Key, in its order");

}

const char* keyName(Key key)
{
    return key < Key::Count ? kKeyNames[size_t(key)] : kKeyNames[0];
}

bool isModifierKey(Key key)
{
    return key >= Key::LeftCtrl && key <= Key::RightSuper;
}

bool InputFrame::pressed(Shortcut s, bool repeats) const
{
    for (const KeyEvent& k : keys)
        if (k.down && k.key == s.key && k.mods == s.mods && (repeats || !k.repeat)) return true;
    return false;
}

bool InputFrame::isDown(Key key) const
{
    return std::find(held.begin(), held.end(), key) != held.end();
}
