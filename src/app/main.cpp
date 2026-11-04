#include <SDL3/SDL_main.h>
#include "app/application.hpp"

#include <string>
#include <string_view>
#include <vector>

// komal [--theme <name>] [images...]
int main(int argc, char** argv)
{
    std::string_view theme = "main-dark";
    std::vector<const char*> files;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--theme" && i + 1 < argc) theme = argv[++i];
        else files.push_back(argv[i]);
    }

    Application app;
    if (!app.init("Komal Studio", 1280, 720, theme))
        return 1;
    for (const char* path : files) app.openFile(path);   // .komal projects open, images are imported
    return app.exec();
}
