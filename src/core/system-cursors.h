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
// under the names Qt and GTK use, preferring ones the user's own theme ships. Elsewhere
// (Wayland, Windows, macOS) SDL's cursors are already the native ones.
//
// It drives the cursor itself on every platform (ImGui's backend is told not to), so a cursor
// made at runtime (a brush outline the size of the brush, see setCustom) has a single owner.
// The system draws its cursor with no frame of latency, unlike anything drawn into the UI.
class SystemCursors {
public:
    void init(SDL_Window* window);   // after ImGui_ImplSDL3_Init*()
    void shutdown();
    void setCustom(SDL_Cursor* cursor) { m_custom = cursor; }   // this frame's, instead of ImGui's
    void update();                   // once per frame, after ImGui::Render()

private:
    void loadThemeCursors(SDL_Window* window);

    static constexpr int kCount = 11;   // ImGuiMouseCursor_COUNT, checked in the .cpp
    SDL_Cursor* m_cursors[kCount] = {};
    SDL_Cursor* m_custom = nullptr;       // asked for this frame
    SDL_Cursor* m_shownCustom = nullptr;  // set on the system now
    int  m_current = -2;
    bool m_active = false;
};
