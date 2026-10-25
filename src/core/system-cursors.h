#pragma once

struct SDL_Window;
struct SDL_Cursor;

// Mouse cursors for ImGui, taken from the desktop's cursor theme.
//
// ImGui's SDL3 backend asks SDL for system cursors. On X11, SDL looks each one up by a
// single CSS name ("ew-resize"). Many themes only ship the older names ("size_hor",
// "sb_h_double_arrow", ...), and libXcursor then quietly takes the cursor from whatever
// theme it falls back to (usually Adwaita), so resize cursors come out in a foreign style;
// with no match at all SDL uses the X core font cursors. On X11 this looks each shape up
// under the names Qt and GTK use, preferring ones the user's own theme ships, and drives
// the cursor itself. Elsewhere (Wayland, Windows) SDL's cursors are already the native
// ones and nothing is changed.
class SystemCursors {
public:
    void init(SDL_Window* window);   // after ImGui_ImplSDL3_Init*()
    void shutdown();
    void update();                   // once per frame, after ImGui::Render()

private:
    static constexpr int kCount = 11;   // ImGuiMouseCursor_COUNT, checked in the .cpp
    SDL_Cursor* m_cursors[kCount] = {};
    int  m_current = -2;
    bool m_active = false;
};
