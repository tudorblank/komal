#include "app/window-icon.hpp"

#include <SDL3/SDL.h>
#include <stb_image.h>

extern const unsigned char kAppIcon[];   // assets/komal-icon-256.png
extern const unsigned int  kAppIconSize;

void setWindowIcon(SDL_Window* window)
{
    int w = 0, h = 0, channels = 0;
    stbi_uc* px = stbi_load_from_memory(kAppIcon, int(kAppIconSize), &w, &h, &channels, 4);
    if (!px) return;
    if (SDL_Surface* icon = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, px, w * 4)) {
        SDL_SetWindowIcon(window, icon);   // copies the pixels
        SDL_DestroySurface(icon);
    }
    stbi_image_free(px);
}
