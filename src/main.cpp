#include <SDL3/SDL_main.h>
#include "core/application.hpp"

int main(int argc, char** argv)
{
    Application app;
    if (!app.init("Komal Studio", 1280, 720))
        return 1;
    for (int i = 1; i < argc; ++i) app.openFile(argv[i]);   // images to import
    return app.exec();
}