#pragma once
// The thumbnail branch of the preview (canvas/preview/preview.hpp): a reached node's output under
// the edit in progress, fitted into a picture (proportions kept) over the pictures' checkerboard,
// rebuilt on the GPU as the edit moves. What a node's thumbnail shows while the edit reaches it,
// and what the navigator shows of the master.
//
//   if (picture.render(preview, node, maxW, maxH, pipelines, encoder, budget) == Result::Rendered)
//       ... picture.texture(): width() x height() from its corner, showing bounds()

#include "canvas/preview/preview-renderer.hpp"

class PreviewPicture {
public:
    enum class Result : uint8_t {
        NotReached,   // no edit, or it doesn't reach the node: nothing to show
        Empty,        // the node shows nothing under the edit
        Waiting,      // leaf tiles still coming in: the last picture stays
        Unchanged,    // the picture already shows this
        Rendered,     // a new picture, recorded into the encoder
    };

    PreviewPicture() = default;
    ~PreviewPicture() { release(); }
    PreviewPicture(const PreviewPicture&) = delete;
    PreviewPicture& operator=(const PreviewPicture&) = delete;

    Result render(const Preview& preview, NodeId node, uint32_t maxW, uint32_t maxH, const CanvasPipelines& p, WGPUCommandEncoder enc,
                  const Deadline& budget);

    bool hasPicture() const { return m_has; }
    const GpuTexture& texture() const { return m_texture; }
    uint32_t width()  const { return m_w; }
    uint32_t height() const { return m_h; }
    BoundsI  bounds() const { return m_bounds; }   // the world rectangle the picture is fitted to
    void release();

private:
    PreviewRenderer m_renderer;
    GpuTexture m_texture;    // the picture, from its corner
    GpuTexture m_retired;    // the one it outgrew: ImGui may still draw it this frame, so it goes with the next
    GpuUniform m_checker;
    GpuObject<WGPUBindGroup> m_resultGroup;
    WGPUTextureView m_groupView = nullptr;   // ... made for this texture
    uint32_t m_w = 0, m_h = 0;
    BoundsI  m_bounds;
    bool     m_has = false;
};
