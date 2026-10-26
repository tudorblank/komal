#pragma once
// The canvas as a scene of items, in the spirit of QGraphicsScene / QGraphicsItem:
//
//   CanvasScene   owns the items and the GPU state they share (CanvasPipelines)
//   CanvasItem    one thing on the canvas, with a z order and visibility (item.h)
//   CanvasView    a camera + render target looking at a scene (view.h)
//
// The view only re-renders its target when something changed: an item (prepare() returning
// true, or update()), the camera, or the size.
//
// Creating items:  RectItem* r = scene.addItem<RectItem>(x, y, w, h, color);
// The scene owns them; removeItem() destroys one. Pointers stay valid until then.

#include "canvas/item.hpp"

#include <cstdint>
#include <memory>
#include <vector>

class CanvasScene {
public:
    bool init(const GpuHandles& ctx);
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
    void drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame);
    void drawOverlays(ImDrawList* dl, const OverlayContext& ctx);

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
