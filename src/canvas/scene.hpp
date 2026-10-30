#pragma once
// The canvas as a scene of items, like QGraphicsScene / QGraphicsItem:
//   CanvasScene   owns the items; they draw with the engine's CanvasPipelines
//   CanvasItem    one thing on the canvas, with z order and visibility (item.hpp)
//   CanvasView    a camera + render target looking at a scene (view.hpp)
// A view re-renders only when an item, the camera or the size changed.
//
//   OutlineItem* o = scene.addItem<OutlineItem>(Palette::kAccent);   // owned by the scene

#include "canvas/item.hpp"

#include <cstdint>
#include <memory>
#include <vector>

class CanvasScene {
public:
    bool init(const CanvasPipelines& pipelines);   // the engine's (CanvasEngine), which outlive it
    void shutdown();   // releases every item's GPU resources
    ~CanvasScene() { shutdown(); }

    const CanvasPipelines& pipelines() const { return *m_pipelines; }

    template<class T, class... Args>
    T* addItem(Args&&... args)
    {
        auto item = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = item.get();
        adopt(std::move(item));
        return raw;
    }
    const std::vector<std::unique_ptr<CanvasItem>>& items() const { return m_items; }

    // Bumps whenever the scene looks different (views compare it with what they rendered).
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

    const CanvasPipelines* m_pipelines = nullptr;
    std::vector<std::unique_ptr<CanvasItem>> m_items;   // z order once sorted
    uint64_t m_revision  = 1;
    uint64_t m_nextOrder = 0;
    bool     m_sortDirty = false;
    bool     m_ready = false;
};
