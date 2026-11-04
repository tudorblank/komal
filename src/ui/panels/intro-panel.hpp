#pragma once
// What the window shows while no document is open, in the dock area's place (under the bars):
// Komal's face, and New and Open under it. Not one of the panels: there's no View toggle for it,
// it's there exactly while nothing else is (UserInterface draws it then).

#include "gpu/texture.hpp"

class ActionRegistry;
class GpuDevice;

class IntroPanel {
public:
    void init(GpuDevice& gpu) { m_gpu = &gpu; }   // the face is made when it's first drawn
    void releaseGpu() { m_face.reset(); m_faceWidth = 0; }
    void restoreGpu(GpuDevice& gpu) { m_gpu = &gpu; }
    void draw(ActionRegistry& actions);

private:
    // The face (assets/icons/komal-face.png, a mask: its alpha) at `width` x its height pixels,
    // averaged down from the source: crisp at any size, as ImGui doesn't mipmap.
    bool makeFace(int width);

    GpuDevice* m_gpu = nullptr;
    GpuTexture m_face;
    int m_faceWidth = 0;    // the texture's, in pixels (0: none)
    float m_aspect = 1.0f;  // the face's height to its width
    bool m_failed = false;  // couldn't be made: buttons alone
};
