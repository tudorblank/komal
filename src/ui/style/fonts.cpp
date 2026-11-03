#include "ui/style/fonts.hpp"

#include <imgui.h>

extern const unsigned char kFontUi[];        // generated, see embed_file() in CMakeLists.txt
extern const unsigned int  kFontUiSize;
extern const unsigned char kFontNumbers[];
extern const unsigned int  kFontNumbersSize;

namespace Fonts {

namespace {

ImFont* g_numbers      = nullptr;
ImFont* g_graph        = nullptr;
ImFont* g_graphNumbers = nullptr;

}

void load()
{
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;   // static data, the atlas must not free it
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontUi), int(kFontUiSize), 0.0f, &cfg);
    g_numbers = atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontNumbers), int(kFontNumbersSize), 0.0f, &cfg);

    ImFontConfig heavy = cfg;
    heavy.RasterizerMultiply = 1.4f;   // glyph coverage brightened: reads as a heavier weight
    g_graph        = atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontUi), int(kFontUiSize), 0.0f, &heavy);
    g_graphNumbers = atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontNumbers), int(kFontNumbersSize), 0.0f, &heavy);
}

ImFont* numbers()      { return g_numbers; }
ImFont* graph()        { return g_graph; }
ImFont* graphNumbers() { return g_graphNumbers; }

Numbers::Numbers()  { ImGui::PushFont(g_numbers, 0.0f); }   // 0: keep the current size
Numbers::~Numbers() { ImGui::PopFont(); }

}
