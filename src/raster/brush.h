#pragma once
// Brush strokes: dabs accumulate in a scratch raster (coverage only), which is then applied
// over the pixels as they were before the stroke. Re-applying from the pre-stroke pixels
// every time means stroke opacity is applied exactly once, however many dabs overlap, and
// Over (paint) / Erase are the ordinary TileEdit::apply modes.
//
//   stroke.begin(params, x, y);    // first dab
//   stroke.moveTo(x, y);           // dabs along the segment, spaced by params.spacing
//   applyStroke(editor, stroke, stroke.takeDirty(), originalTileFn);
//   ...
//   stroke.end();

#include "raster/raster-data.h"

#include <functional>

struct StrokePoint { float x, y; };   // world (pixel) coordinates

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
};

class BrushStroke {
public:
    void begin(const BrushParams& params, float x, float y);   // world (pixel) coordinates
    void moveTo(float x, float y);
    void end();   // drops the scratch
    bool active() const { return m_active; }
    const BrushParams& params() const { return m_params; }

    TileSet takeDirty() { return std::exchange(m_dirty, TileSet{}); }   // scratch tiles changed since the last call
    const TileGrid& scratch() const { return m_scratch; }               // coverage in every channel (white, premultiplied)

    // `out` = `original` (pre-stroke, may be null) with the stroke applied. False if empty.
    bool composeTile(Tile& out, const Tile* original, int tx, int ty) const;

private:
    void buildDabTable();
    void stampDab(float cx, float cy);
    float coverage(float distance) const;   // 0..1 at a pixel centre this far from the dab centre

    static constexpr int kDabTable = 4096;

    BrushParams m_params;
    float    m_dabTable[kDabTable + 1] = {};   // coverage * flow (0..255) by squared distance (see buildDabTable)
    float    m_dabToIndex = 0;
    float    m_dabRadius  = 0;
    float    m_hardR2     = 0;    // not antialiased: inside where d^2 <= this
    uint8_t  m_flowK      = 0;
    RGBA     m_color{};          // premultiplied stroke colour
    TileGrid m_scratch;          // the stroke so far
    TileSet  m_dirty;
    float    m_lastX = 0, m_lastY = 0;
    float    m_sinceDab = 0;     // distance travelled since the last dab
    bool     m_active = false;
    mutable std::unique_ptr<Tile> m_colored = std::make_unique<Tile>();   // composeTile scratch
};

// Re-applies `stroke` to `tiles` of the raster being edited. original(tx, ty) must return the
// tile as it was before the stroke began (nullptr if it didn't exist).
void applyStroke(RasterEditor& ed, const BrushStroke& stroke, const TileSet& tiles,
                 const std::function<const Tile*(int tx, int ty)>& original);
