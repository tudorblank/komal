#pragma once
// Equal slots in pages of square textures: where many small pictures share a few big textures
// (tiles in an atlas, node thumbnails). Slots are handed out lowest first, a new page made when
// every slot is taken. A freed slot is reused only after recycle(): whatever drew from it this
// frame still reads its old pixels when the frame's commands run.
//
//   SlotPages pages({ .slotWidth = 66, .slotHeight = 66, .format = ..., .usage = ... });
//   const SlotPages::Slot s = pages.allocate(device);
//   pages.texture(s.page);  pages.originX(s);  pages.originY(s);   // where it is

#include "gfx/texture.hpp"

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
    void recycle();                     // freed slots back in use: once a frame, before anything draws
    void release();                     // every page; the slots handed out must be dropped first

    const Config& config() const { return m_config; }
    int slotsPerRow()  const { return m_config.pageSize / m_config.slotWidth; }
    int slotsPerPage() const { return slotsPerRow() * (m_config.pageSize / m_config.slotHeight); }
    int originX(Slot s) const { return (s.slot % slotsPerRow()) * m_config.slotWidth; }    // in its page, pixels
    int originY(Slot s) const { return (s.slot / slotsPerRow()) * m_config.slotHeight; }

    size_t pageCount() const { return m_pages.size(); }
    const GpuTexture& texture(int page) const { return m_pages[size_t(page)].texture; }

private:
    struct Page {
        GpuTexture texture;
        std::vector<uint16_t> freeSlots;   // popped from the back: low slots first
        std::vector<uint16_t> freed;       // until recycle()
    };

    Config m_config;
    std::vector<Page> m_pages;
};
