#include <SDL3/SDL_main.h>
#include "app/application.hpp"

#include <string>
#include <string_view>
#include <vector>

// komal [--theme <name>] [images...]
int main(int argc, char** argv)
{
    std::string_view theme = "main-dark";
    std::vector<const char*> images;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--theme" && i + 1 < argc) theme = argv[++i];
        else images.push_back(argv[i]);
    }

    Application app;
    if (!app.init("Komal Studio", 1280, 720, theme))
        return 1;
    for (const char* path : images) app.openFile(path);   // images to import
    return app.exec();
}
