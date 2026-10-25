#pragma once
// The canvas as a scene of items, in the spirit of QGraphicsScene / QGraphicsItem:
//
//   CanvasScene   owns the items and the GPU state they share (CanvasPipelines)
//   CanvasItem    one thing on the canvas, with a z order and visibility
//   CanvasView    a camera + render target looking at a scene (canvas-view.h)
//
// An item can take part in up to three stages, each optional:
//   prepare()       CPU side, every frame before rendering: upload changed data, catch up
//                   with its source (within a shared time budget). Returns "I changed".
//   draw()          GPU, in world space, inside the view's canvas pass (z order)
//   paintOverlay()  ImGui, in screen space, over the rendered canvas (z order): outlines,
//                   cursors, handles; always crisp, never needs a re-render
// The view only re-renders its target when something changed: an item (prepare() returning
// true, or update()), the camera, or the size.
//
// Creating items:  RectItem* r = scene.addItem<RectItem>(x, y, w, h, color);
// The scene owns them; removeItem() destroys one. Pointers stay valid until then.

#include "gfx/canvas-camera.h"
#include "gfx/canvas-pipelines.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

struct ImDrawList;
class CanvasScene;

// What a view hands to items while it renders a frame.
class CanvasFrame {
public:
    struct Target {
        WGPUCommandEncoder encoder;           // records before the view's own pass begins
        float    viewW, viewH;                // view size in points (the camera's units)
        uint32_t pixelW, pixelH;              // the view's target in pixels
    };
    CanvasFrame(const CanvasPipelines& p, const CanvasCamera& cam, ViewRect visible, Target t, double budgetMs)
        : pipelines(p), camera(cam), view(visible), target(t), m_deadline(Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                                                    std::chrono::duration<double, std::milli>(budgetMs))) {}

    const CanvasPipelines& pipelines;
    const CanvasCamera&    camera;
    const ViewRect         view;     // world rectangle on screen
    const Target           target;   // prepare() may record its own passes into target.encoder

    // CPU time left for prepare() work this frame, shared by every item.
    bool inBudget() const { return Clock::now() < m_deadline; }

    // Binds `pipeline` and the camera, unless they're bound already (consecutive items of a
    // kind share both).
    void bind(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline)
    {
        if (pipeline == m_bound) return;
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, camera.bindGroup(), 0, nullptr);
        m_bound = pipeline;
    }
    // Binds a pipeline that brings its own group 0 (no camera); the next bind() rebinds both.
    void bindOwn(WGPURenderPassEncoder pass, WGPURenderPipeline pipeline)
    {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        m_bound = pipeline;
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point  m_deadline;
    WGPURenderPipeline m_bound = nullptr;
};

// What overlay painting gets: the view's camera and where it sits on screen.
struct OverlayContext {
    const CanvasCamera& camera;
    float originX, originY;   // screen position of the view's top-left corner
    float width, height;      // view size in points

    Vec2 toScreen(float wx, float wy) const
    {
        const Vec2 s = camera.worldToScreen({ wx, wy });
        return { originX + s.x, originY + s.y };
    }
};

class CanvasItem {
public:
    CanvasItem() = default;
    virtual ~CanvasItem() = default;
    CanvasItem(const CanvasItem&) = delete;
    CanvasItem& operator=(const CanvasItem&) = delete;

    CanvasScene* scene() const { return m_scene; }

    float z() const { return m_z; }
    void  setZ(float z);
    bool  visible() const { return m_visible; }
    void  setVisible(bool visible);

    // ---- what an item implements (all optional) ----
    virtual bool prepare(CanvasFrame&) { return false; }
    virtual void draw(WGPURenderPassEncoder, CanvasFrame&) {}
    virtual void paintOverlay(ImDrawList*, const OverlayContext&) {}
    virtual void releaseGpu() {}   // drop GPU resources; prepare() may recreate them later
    // False for overlay-only items: showing, hiding or re-ordering them then costs no re-render.
    virtual bool drawsOnGpu() const { return true; }
    // False while what the item would draw on screen is half-way through an update (some
    // visible tiles new, some old): views keep their last complete picture meanwhile.
    virtual bool settled() const { return true; }

protected:
    void update();   // "I look different now": views re-render on their next frame

private:
    friend class CanvasScene;
    CanvasScene* m_scene = nullptr;
    float    m_z = 0.0f;
    uint64_t m_order = 0;   // insertion order: ties in z keep it
    bool     m_visible = true;
};

class CanvasScene {
public:
    bool init(const GfxContext& ctx);
    void shutdown();   // releases every item's GPU resources and the pipelines
    ~CanvasScene() { shutdown(); }

    const CanvasPipelines& pipelines() const { return m_pipelines; }

    template<class T, class... Args>
    T* addItem(Args&&... args)
    {
        auto item = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = item.get();
        adopt(std::move(item));
        return raw;
    }
    void removeItem(CanvasItem* item);
    const std::vector<std::unique_ptr<CanvasItem>>& items() const { return m_items; }

    // Bumps whenever something in the scene looks different. Views compare it with the
    // revision they last rendered.
    uint64_t revision() const { return m_revision; }

    // Driven by views, every frame:
    bool prepare(CanvasFrame& frame);                          // true if anything changed
    bool settled() const;                                      // every visible item settled
    void draw(WGPURenderPassEncoder pass, CanvasFrame& frame);
    void paintOverlays(ImDrawList* dl, const OverlayContext& ctx);

private:
    friend class CanvasItem;
    void adopt(std::unique_ptr<CanvasItem> item);
    void touch() { ++m_revision; }
    void sortIfNeeded();

    CanvasPipelines m_pipelines;
    std::vector<std::unique_ptr<CanvasItem>> m_items;   // z order once sorted
    uint64_t m_revision  = 1;
    uint64_t m_nextOrder = 0;
    bool     m_sortDirty = false;
    bool     m_ready = false;
};
