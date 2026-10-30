#include "brush.hpp"

#include <algorithm>
#include <cmath>

namespace {

float smoothstep(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

int clampWorld(float v)
{
    return (int)std::clamp(std::floor(v), (float)-TileUtils::kWorldLimit, (float)TileUtils::kWorldLimit);
}

}

void BrushStroke::begin(const BrushParams& params, StrokePoint at)
{
    end();
    m_params = params;
    m_params.diameter = std::max(params.diameter, 1.0f);
    m_params.spacing  = std::max(params.spacing, 0.01f);
    const RGBA c = params.color;
    m_color = canonical({ mul255(c.r, c.a), mul255(c.g, c.a), mul255(c.b, c.a), c.a });   // premultiply
    m_flowK = uint8_t(std::clamp(int(m_params.flow * 255.0f + 0.5f), 0, 255));
    m_shapes.clear();
    m_shapes.resize(kSizeLevels + 1);
    m_active = true;
    m_last = at;
    m_sinceDab = 0;
    stampDab(at.x, at.y, at.pressure);
}

void BrushStroke::moveTo(StrokePoint to)
{
    if (!m_active) return;
    const float dx = to.x - m_last.x, dy = to.y - m_last.y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist <= 0.0f) { m_last.pressure = to.pressure; return; }

    // A dab every `step` pixels (following the dab size there); the remainder carries into the
    // next segment, so spacing stays even. Never over half a diameter apart: no gaps.
    const auto pressureAt = [&](float along) { return m_last.pressure + (to.pressure - m_last.pressure) * (along / dist); };
    const auto stepAt = [&](float pressure) { return std::max(0.5f, std::min(m_params.spacing, 0.5f) * diameterAt(pressure)); };
    const float ux = dx / dist, uy = dy / dist;
    float at = 0.0f, since = m_sinceDab;
    for (;;) {
        const float next = at + std::max(0.0f, stepAt(pressureAt(at)) - since);
        if (next > dist) break;
        at = next;
        since = 0.0f;
        stampDab(m_last.x + ux * at, m_last.y + uy * at, pressureAt(at));
        if (at == dist) break;
    }
    m_sinceDab = since + (dist - at);
    m_last = to;
}

void BrushStroke::end()
{
    m_scratch.clear();
    m_dirty.clear();
    m_shapes.clear();
    m_active = false;
}

int BrushStroke::sizeLevel(float pressure) const
{
    return m_params.pressureSize ? std::clamp(int(pressure * kSizeLevels + 0.5f), 1, kSizeLevels) : kSizeLevels;
}

float BrushStroke::diameterAt(float pressure) const
{
    return std::max(1.0f, m_params.diameter * float(sizeLevel(pressure)) / kSizeLevels);
}

float BrushStroke::coverage(float d, float diameter) const
{
    const float r = diameter * 0.5f;
    if (!m_params.antialias)
        return d <= std::max(r, 0.5f) ? 1.0f : 0.0f;
    const float inner   = r * std::clamp(m_params.hardness, 0.0f, 1.0f);
    const float soft    = r - inner;   // 0 at hardness 1: then only the rim softens the edge
    const float falloff = d <= inner || soft < 1e-3f ? 1.0f : 1.0f - smoothstep((d - inner) / soft);
    const float edge    = std::clamp(r + 0.5f - d, 0.0f, 1.0f);   // 1 px antialiased rim
    return falloff * edge;
}

const BrushStroke::DabShape& BrushStroke::dabShape(int level)
{
    std::unique_ptr<DabShape>& slot = m_shapes[size_t(level)];
    if (slot) return *slot;
    slot = std::make_unique<DabShape>();
    DabShape& s = *slot;
    const float diameter = std::max(1.0f, m_params.diameter * float(level) / kSizeLevels);
    s.radius  = diameter * 0.5f + 0.5f;   // + half a pixel: the antialiased rim
    s.toIndex = float(kDabTable - 1) / (s.radius * s.radius);
    s.hardR2  = std::max(diameter * 0.5f, 0.5f) * std::max(diameter * 0.5f, 0.5f);
    for (int i = 0; i < kDabTable; ++i)
        s.table[i] = coverage(std::sqrt(float(i) / s.toIndex), diameter) * float(m_flowK);
    s.table[kDabTable] = 0.0f;   // so interpolating at the very edge reads past nothing
    return s;
}

void BrushStroke::stampDab(float cx, float cy, float pressure)
{
    const float flowScale = m_params.pressureFlow ? std::clamp(pressure, 0.0f, 1.0f) : 1.0f;
    if (m_flowK == 0 || flowScale <= 0.0f) return;
    const DabShape& shape = dabShape(sizeLevel(pressure));
    const float r = shape.radius;
    const BoundsI area{ clampWorld(cx - r), clampWorld(cy - r), clampWorld(cx + r), clampWorld(cy + r) };
    const float r2 = r * r;
    const int hardK = int(float(m_flowK) * flowScale + 0.5f);

    forEachTileSpan(area, [&](const TileSpan& s) {
        Tile* existing = m_scratch.getWriteTile(s.tx, s.ty);
        Tile& tile = existing ? *existing : m_scratch.writeOrCreateTile(s.tx, s.ty);
        bool changed = false;
        {
            Tile::BulkWrite w(tile, Tile::BulkWrite::Trusted);
            RGBA* px = w.data();
            for (int ly = s.ly0; ly <= s.ly1; ++ly) {
                const float py = float(s.tileWY + ly) + 0.5f - cy;
                const float rowR2 = r2 - py * py;
                if (rowR2 <= 0.0f) continue;
                // Only the part of the row inside the dab.
                const float half = std::sqrt(rowR2);
                const int lx0 = std::max(s.lx0, int(std::floor(cx - half - 0.5f)) - s.tileWX);
                const int lx1 = std::min(s.lx1, int(std::ceil(cx + half - 0.5f)) - s.tileWX);
                RGBA* row = px + ly * Tile::kSize;
                for (int lx = lx0; lx <= lx1; ++lx) {
                    RGBA& p = row[lx];
                    if (p.a == 255) continue;
                    const float pxd = float(s.tileWX + lx) + 0.5f - cx;
                    const float d2 = pxd * pxd + py * py;
                    if (d2 >= r2) continue;
                    int k;
                    if (m_params.antialias) {
                        const float f = d2 * shape.toIndex;
                        const int   i = int(f);
                        k = int((shape.table[i] + (shape.table[i + 1] - shape.table[i]) * (f - float(i))) * flowScale + 0.5f);
                    } else {
                        k = d2 <= shape.hardR2 ? hardK : 0;   // a hard edge: exact
                    }
                    if (k == 0) continue;
                    const int a = std::min(255, p.a + (k * (255 - p.a) + 127) / 255);   // coverage builds up
                    if (a == p.a) continue;
                    p = { uint8_t(a), uint8_t(a), uint8_t(a), uint8_t(a) };
                    changed = true;
                }
            }
            if (changed) w.markChanged();
        }
        if (changed) m_dirty.insert(TileUtils::packKey(s.tx, s.ty));
        else if (!existing) m_scratch.freeTile(s.tx, s.ty);   // the dab only grazed it
    });
}

bool BrushStroke::composeTile(Tile& out, const Tile* original, int tx, int ty) const
{
    out.clear();
    if (original) out.copyFrom(*original);
    const Tile* cov = m_scratch.getReadTile(tx, ty);
    if (!cov) return !out.isEmpty();

    // coverage -> stroke colour (for Erase only the alpha matters)
    Tile& colored = *m_colored;
    colored.clear();
    {
        Tile::BulkWrite w(colored, Tile::BulkWrite::Trusted);
        const RGBA* c = cov->getReadAll();
        RGBA* d = w.data();
        for (int i = 0; i < Tile::kPixels; ++i)
            if (c[i].a) d[i] = scale(m_color, c[i].a);
        w.markChanged();
    }
    if (m_params.keepAlpha && (m_params.mode == CompositeMode::Erase || !original)) return !out.isEmpty();
    TileEdit::apply(out, colored, { m_params.opacity, m_params.mode });
    if (m_params.keepAlpha) {
        // Back to the original alpha, keeping the new colour: premultiplied, that rescales rgb.
        Tile::BulkWrite w(out, Tile::BulkWrite::Trusted);
        RGBA* d = w.data();
        const RGBA* o = original->getReadAll();
        for (int i = 0; i < Tile::kPixels; ++i) {
            if (d[i].a == o[i].a) continue;
            const int a = o[i].a, n = d[i].a;   // n >= a: painting Over only adds coverage
            d[i] = a == 0 ? RGBA{ 0, 0, 0, 0 }
                          : RGBA{ uint8_t((d[i].r * a + n / 2) / n), uint8_t((d[i].g * a + n / 2) / n),
                                  uint8_t((d[i].b * a + n / 2) / n), uint8_t(a) };
        }
        w.markChanged();
    }
    return !out.isEmpty();
}

void applyStroke(RasterEditor& editor, const RasterData& raster, const BrushStroke& stroke, const TileSet& tiles)
{
    auto composed = std::make_unique<Tile>();
    for (TileKey key : tiles) {
        const auto [tx, ty] = TileUtils::unpackKey(key);
        if (stroke.composeTile(*composed, raster.getReadTile(tx, ty), tx, ty))   // read before it's written
            editor.applyTile(tx, ty, *composed, { 255, CompositeMode::Replace });
        else
            editor.eraseTile(tx, ty);   // erased down to nothing
    }
}
