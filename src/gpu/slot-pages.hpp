#pragma once
// Equal slots in pages of square textures: where many small pictures share a few big textures
// (tiles in an atlas, node thumbnails). Slots are handed out lowest first, a new page made when
// every slot is taken. A freed slot is reused only after recycle(): whatever drew from it this
// frame still reads its old pixels when the frame's commands run. A page left empty for a while
// (Config::keepEmpty) gives its texture back at a recycle(); it's made again when it's needed.
//
//   SlotPages pages({ .slotWidth = 66, .slotHeight = 66, .format = ..., .usage = ... });
//   const SlotPages::Slot s = pages.allocate(device);
//   pages.texture(s.page);  pages.originX(s);  pages.originY(s);   // where it is

#include "base/clock.hpp"
#include "gpu/texture.hpp"

#include <cstdint>
#include <vector>

class SlotPages {
public:
    struct Config {
        int               slotWidth = 64, slotHeight = 64;   // pixels
        int               pageSize = 2048;                   // a page's side, pixels
        WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
        WGPUTextureUsage  usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        const char*       label = "Slot Page";
        Clock::Seconds    keepEmpty{ 5.0 };   // how long an empty page keeps its texture (churn: freed and filled again)
    };
    struct Slot {
        int page = -1, slot = -1;
        explicit operator bool() const { return page >= 0; }
    };

    SlotPages() = default;
    explicit SlotPages(const Config& config) : m_config(config) {}
    void configure(const Config& config);   // before the first allocate()

    Slot allocate(WGPUDevice device);   // an invalid Slot if a new page couldn't be made
    void free(Slot s);                  // reused after the next recycle()
    void freeNow(Slot s);               // no view drew it this frame
    void recycle();                     // freed slots back in use, long-empty pages released: once a frame, before anything draws
    void release();                     // every page; the slots handed out must be dropped first

    const Config& config() const { return m_config; }
    int slotsPerRow()  const { return m_config.pageSize / m_config.slotWidth; }
    int slotsPerPage() const { return slotsPerRow() * (m_config.pageSize / m_config.slotHeight); }
    int originX(Slot s) const { return (s.slot % slotsPerRow()) * m_config.slotWidth; }    // in its page, pixels
    int originY(Slot s) const { return (s.slot / slotsPerRow()) * m_config.slotHeight; }

    size_t pageCount() const { return m_pages.size(); }   // pages that gave their texture back included: their slots keep their place
    const GpuTexture& texture(int page) const { return m_pages[size_t(page)].texture; }   // empty if it gave it back
    size_t bytes() const;   // the textures held

private:
    struct Page {
        GpuTexture texture;
        std::vector<uint16_t> freeSlots;   // popped from the back: low slots first
        std::vector<uint16_t> freed;       // until recycle()
        Clock::TimePoint emptySince{};     // every slot free since then (none: in use)
    };
    bool makeTexture(WGPUDevice device, Page& page) const;

    Config m_config;
    std::vector<Page> m_pages;
};
