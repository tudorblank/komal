#pragma once
// Brush strokes: dabs accumulate coverage in a scratch grid, re-applied over the raster's
// committed pixels into its draft each time. So stroke opacity applies exactly once however many
// dabs overlap, and paint / erase are plain TileEdit::apply modes.
//
//   stroke.begin(params, point);                       // first dab
//   stroke.moveTo(point);                              // dabs along the segment, spaced by params.spacing
//   applyStroke(raster, stroke, stroke.takeDirty());   // into the raster's draft
//   ...
//   stroke.end();
//   raster.commitDraft();                              // one commit in the raster's history
//
// Pressure (1 for a mouse) can scale dab size and flow, interpolated along each segment.

#include "raster/raster-data.hpp"

#include <memory>
#include <vector>

struct StrokePoint {
    float x = 0.0f, y = 0.0f;   // world (pixel) coordinates
    float pressure = 1.0f;      // 0..1
};

struct BrushParams {
    float         diameter  = 8.0f;    // pixels
    float         hardness  = 0.8f;    // 0 = soft all the way from the centre, 1 = solid to the edge
    float         flow      = 1.0f;    // coverage each dab adds (builds up within the stroke)
    float         spacing   = 0.15f;   // distance between dabs, as a fraction of the diameter
    bool          antialias = true;    // off: hard pixel edges (pixel art; diameter 1 = single pixels)
    RGBA          color     = { 255, 255, 255, 255 };   // straight alpha (as from a colour picker)
    uint8_t       opacity   = 255;     // applied once to the whole stroke
    CompositeMode mode      = CompositeMode::Over;      // Over paints, Erase erases
    bool          keepAlpha = false;   // lock transparency: colour changes, alpha never (Erase does nothing)
    bool          pressureSize = true;    // pressure scales the diameter
    bool          pressureFlow = false;   // pressure scales the flow
};

class BrushStroke {
public:
    void begin(const BrushParams& params, StrokePoint at);
    void moveTo(StrokePoint to);
    void end();   // drops the scratch
    bool active() const { return m_active; }
    const BrushParams& params() const { return m_params; }

    TileSet takeDirty() { return std::exchange(m_dirty, TileSet{}); }   // scratch tiles changed since the last call
    const TileGrid& scratch() const { return m_scratch; }               // coverage in every channel (white, premultiplied)

    // `out` = `original` (pre-stroke, may be null) with the stroke applied. False if empty.
    bool composeTile(Tile& out, const Tile* original, int tx, int ty) const;

private:
    static constexpr int kDabTable  = 4096;
    static constexpr int kSizeLevels = 64;   // pressure steps of the diameter

    // One dab size: coverage * flow (0..255) by squared distance, so stamping needs no sqrt per
    // pixel. Built on first use, per size level.
    struct DabShape {
        float radius  = 0.0f;   // reach, rim included
        float toIndex = 0.0f;   // squared distance -> table index
        float hardR2  = 0.0f;   // not antialiased: inside where d^2 <= this
        float table[kDabTable + 1];
    };

    int   sizeLevel(float pressure) const;
    float diameterAt(float pressure) const;
    const DabShape& dabShape(int level);
    void  stampDab(float cx, float cy, float pressure);
    float coverage(float distance, float diameter) const;   // 0..1 at a pixel centre this far from the dab centre

    BrushParams m_params;
    std::vector<std::unique_ptr<DabShape>> m_shapes;   // by size level
    uint8_t  m_flowK = 0;
    RGBA     m_color{};          // premultiplied stroke colour
    TileGrid m_scratch;          // the stroke so far
    TileSet  m_dirty;
    StrokePoint m_last;
    float    m_sinceDab = 0;     // distance travelled since the last dab
    bool     m_active = false;
    mutable std::unique_ptr<Tile> m_colored = std::make_unique<Tile>();   // composeTile scratch
};

// Recomposes `tiles` of the raster's draft: its committed pixels with the stroke applied.
void applyStroke(RasterData& raster, const BrushStroke& stroke, const TileSet& tiles);
