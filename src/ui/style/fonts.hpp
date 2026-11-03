#pragma once
// The UI's fonts, baked into the binary (CMakeLists.txt): Manrope for text, JetBrains Mono for
// numbers you can type in, and heavier cuts of both for the graph, drawn small on a dark canvas.
//
//   Fonts::load();               // once, after ImGui::CreateContext()
//   { Fonts::Numbers numbers; ... }   // numbers in this scope

struct ImFont;

namespace Fonts {

void    load();
ImFont* numbers();        // values in fields
ImFont* graph();          // the graph's labels
ImFont* graphNumbers();   // ... and its values

// The number font for a scope, at the current size.
class Numbers {
public:
    Numbers();
    ~Numbers();
    Numbers(const Numbers&) = delete;
    Numbers& operator=(const Numbers&) = delete;
};

}
