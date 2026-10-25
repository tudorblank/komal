#include <SDL3/SDL_main.h>   // portable entry point (WinMain etc.)
#include "core/application.h"

int main(int argc, char** argv)
{
    Application app;
    if (!app.init("MyApp", 1280, 720))
        return 1;
    for (int i = 1; i < argc; ++i) app.openFile(argv[i]);   // images to import
    return app.exec();
}
