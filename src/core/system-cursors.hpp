#pragma once

struct SDL_Window;
struct SDL_Cursor;

// Mouse cursors for ImGui from the desktop's theme, and the one owner of the system cursor
// (ImGui's backend is told not to touch it), so runtime cursors (setCustom) don't fight it.
//
// On X11, SDL looks cursors up by one CSS name, which many themes lack, falling back to a foreign
// theme. This tries the names Qt and GTK use, preferring ones the user's theme ships. Elsewhere
// SDL's cursors are already native.
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
