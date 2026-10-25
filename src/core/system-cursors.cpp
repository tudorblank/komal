#include "system-cursors.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

static_assert(ImGuiMouseCursor_COUNT == 11, "update SystemCursors::kCount and the tables below");

namespace {

// libXcursor, loaded at runtime (no build dependency; SDL loads the same library).
struct XcursorImage {
    unsigned int  version, size, width, height, xhot, yhot, delay;
    unsigned int* pixels;   // premultiplied ARGB
};
using LoadImageFn  = XcursorImage* (*)(const char* name, const char* theme, int size);
using DestroyFn    = void (*)(XcursorImage*);
using GetThemeFn   = char* (*)(void* display);
using DefaultSizeFn = int (*)(void* display);
using LibraryPathFn = const char* (*)();

// Whether `theme` itself ships cursor `name` (not just something it falls back to).
bool themeShips(const char* searchPath, const char* theme, const char* name)
{
    if (!searchPath || !theme) return false;
    const char* home = std::getenv("HOME");
    std::string path = searchPath;
    for (size_t start = 0; start <= path.size();) {
        size_t end = path.find(':', start);
        if (end == std::string::npos) end = path.size();
        std::string dir = path.substr(start, end - start);
        if (!dir.empty() && dir[0] == '~' && home) dir = home + dir.substr(1);
        std::error_code ec;
        if (!dir.empty() && std::filesystem::exists(std::filesystem::path(dir) / theme / "cursors" / name, ec))
            return true;
        start = end + 1;
    }
    return false;
}

// Theme names to try per ImGuiMouseCursor, freedesktop first, then the legacy X11 names.
const char* const kNames[][5] = {
    { "default", "left_ptr" },                                         // Arrow
    { "text", "xterm", "ibeam" },                                      // TextInput
    { "all-scroll", "move", "fleur", "size_all" },                     // ResizeAll
    { "ns-resize", "size_ver", "sb_v_double_arrow", "v_double_arrow", "row-resize" },   // ResizeNS
    { "ew-resize", "size_hor", "sb_h_double_arrow", "h_double_arrow", "col-resize" },   // ResizeEW
    { "nesw-resize", "size_bdiag", "fd_double_arrow" },                // ResizeNESW
    { "nwse-resize", "size_fdiag", "bd_double_arrow" },                // ResizeNWSE
    { "pointer", "hand2", "pointing_hand", "hand1" },                  // Hand
    { "wait", "watch" },                                               // Wait
    { "progress", "left_ptr_watch", "half-busy" },                     // Progress
    { "not-allowed", "crossed_circle", "forbidden" },                  // NotAllowed
};

// Same mapping as imgui_impl_sdl3, used for anything the theme lookup can't provide.
const SDL_SystemCursor kSystem[] = {
    SDL_SYSTEM_CURSOR_DEFAULT, SDL_SYSTEM_CURSOR_TEXT, SDL_SYSTEM_CURSOR_MOVE,
    SDL_SYSTEM_CURSOR_NS_RESIZE, SDL_SYSTEM_CURSOR_EW_RESIZE, SDL_SYSTEM_CURSOR_NESW_RESIZE,
    SDL_SYSTEM_CURSOR_NWSE_RESIZE, SDL_SYSTEM_CURSOR_POINTER, SDL_SYSTEM_CURSOR_WAIT,
    SDL_SYSTEM_CURSOR_PROGRESS, SDL_SYSTEM_CURSOR_NOT_ALLOWED,
};

SDL_Cursor* cursorFromImage(const XcursorImage* img)
{
    SDL_Surface* surface = SDL_CreateSurface((int)img->width, (int)img->height, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return nullptr;
    // Xcursor pixels are premultiplied, SDL color cursors take straight alpha.
    for (unsigned int y = 0; y < img->height; ++y) {
        auto* row = (Uint32*)((Uint8*)surface->pixels + y * surface->pitch);
        for (unsigned int x = 0; x < img->width; ++x) {
            const Uint32 p = img->pixels[y * img->width + x];
            const Uint32 a = p >> 24;
            Uint32 r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
            if (a != 0 && a != 255) { r = r * 255 / a; g = g * 255 / a; b = b * 255 / a; }
            row[x] = (a << 24) | (SDL_min(r, 255u) << 16) | (SDL_min(g, 255u) << 8) | SDL_min(b, 255u);
        }
    }
    SDL_Cursor* cursor = SDL_CreateColorCursor(surface, (int)img->xhot, (int)img->yhot);
    SDL_DestroySurface(surface);
    return cursor;
}

} // namespace

void SystemCursors::init(SDL_Window* window)
{
    const char* driver = SDL_GetCurrentVideoDriver();
    if (!driver || std::strcmp(driver, "x11") != 0)
        return;

    void* display = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                           SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    SDL_SharedObject* lib = SDL_LoadObject("libXcursor.so.1");
    if (!display || !lib) {
        if (lib) SDL_UnloadObject(lib);
        return;
    }
    const auto loadImage   = (LoadImageFn)SDL_LoadFunction(lib, "XcursorLibraryLoadImage");
    const auto destroy     = (DestroyFn)SDL_LoadFunction(lib, "XcursorImageDestroy");
    const auto getTheme    = (GetThemeFn)SDL_LoadFunction(lib, "XcursorGetTheme");
    const auto defaultSize = (DefaultSizeFn)SDL_LoadFunction(lib, "XcursorGetDefaultSize");
    const auto libraryPath = (LibraryPathFn)SDL_LoadFunction(lib, "XcursorLibraryPath");

    if (loadImage && destroy && getTheme && defaultSize && libraryPath) {
        const char* theme = getTheme(display);   // Xcursor.theme / XCURSOR_THEME
        const int   size  = defaultSize(display);
        const char* path  = libraryPath();
        const auto load = [&](const char* name) -> SDL_Cursor* {
            XcursorImage* img = loadImage(name, theme, size);
            if (!img) return nullptr;
            SDL_Cursor* cursor = cursorFromImage(img);
            destroy(img);
            return cursor;
        };
        for (int i = 0; i < kCount; ++i) {
            // First a name the user's theme ships itself, then anything it falls back to.
            for (const char* name : kNames[i])
                if (name && !m_cursors[i] && themeShips(path, theme, name))
                    m_cursors[i] = load(name);
            for (const char* name : kNames[i])
                if (name && !m_cursors[i])
                    m_cursors[i] = load(name);
            if (!m_cursors[i])
                m_cursors[i] = SDL_CreateSystemCursor(kSystem[i]);
        }
        m_active = true;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;   // we drive the cursor
    }
    SDL_UnloadObject(lib);   // the cursors are plain SDL cursors now
}

void SystemCursors::shutdown()
{
    if (!m_active) return;
    SDL_SetCursor(SDL_GetDefaultCursor());
    for (SDL_Cursor*& c : m_cursors) {
        if (c) SDL_DestroyCursor(c);
        c = nullptr;
    }
    m_active = false;
}

void SystemCursors::update()
{
    if (!m_active) return;
    const ImGuiMouseCursor wanted = ImGui::GetIO().MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
    if (wanted == m_current) return;
    m_current = wanted;

    if (wanted == ImGuiMouseCursor_None || wanted < 0 || wanted >= kCount) {
        SDL_HideCursor();
        return;
    }
    SDL_SetCursor(m_cursors[wanted] ? m_cursors[wanted] : m_cursors[ImGuiMouseCursor_Arrow]);
    SDL_ShowCursor();
}
