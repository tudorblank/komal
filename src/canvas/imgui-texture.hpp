#pragma once
// A GPU texture as ImGui draws it (ImGui's wgpu backend takes the view as the texture id).

#include <imgui.h>
#include <webgpu/webgpu.h>

#include <cstdint>

inline ImTextureID imguiTexture(WGPUTextureView view) { return ImTextureID(reinterpret_cast<uintptr_t>(view)); }

// Where a w x h picture at (x, y) sits in a texW x texH texture, as UVs half a texel in on every
// side: ImGui samples linearly, so the picture's edge never blends in what's beside it (another
// picture in a shared page, or what's past it).
struct ImageUv {
    ImVec2 uv0, uv1;
};
inline ImageUv pictureUv(float x, float y, float w, float h, float texW, float texH)
{
    return { ImVec2((x + 0.5f) / texW, (y + 0.5f) / texH), ImVec2((x + w - 0.5f) / texW, (y + h - 0.5f) / texH) };
}
