#pragma once
// A render pass, open for the scope: every pass the app records begins and ends through it.
//
//   {
//       GpuRenderPass pass(encoder, target, GpuRenderPass::Load::Clear, "Thumbnails");
//       pass.confine(x, y, w, h);              // draw only there (viewport and scissor)
//       wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
//   }                                          // ended

#include "gfx/gpu-object.hpp"

#include <cstdint>
#include <span>

class GpuRenderPass {
public:
    enum class Load : uint8_t { Clear, Keep };   // what the target starts with: cleared, or as it was

    // `clear`: the colour a Clear pass starts with (null: transparent).
    GpuRenderPass(WGPUCommandEncoder encoder, WGPUTextureView target, Load load, const char* label, const float* clear = nullptr);
    ~GpuRenderPass();
    GpuRenderPass(const GpuRenderPass&) = delete;
    GpuRenderPass& operator=(const GpuRenderPass&) = delete;

    void confine(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const;   // the viewport, and a scissor to match
    operator WGPURenderPassEncoder() const { return m_pass; }

private:
    WGPURenderPassEncoder m_pass = nullptr;
};

// Textures at bindings 0, 1, ... of a group of `layout` (no samplers): what full-screen passes read.
GpuObject<WGPUBindGroup> textureBindGroup(WGPUDevice device, WGPUBindGroupLayout layout, std::span<const WGPUTextureView> views,
                                          const char* label);
