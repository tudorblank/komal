#include "brush.h"

#include <algorithm>
#include <cmath>

namespace {

float smoothstep(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

int clampWorld(float v)
{
    return (int)std::clamp(std::floor(v), (float)-TileUtils::kWorldLimit, (float)TileUtils::kWorldLimit);
}

} // namespace

void BrushStroke::begin(const BrushParams& params, float x, float y)
{
    end();
    m_params = params;
    m_params.diameter = std::max(params.diameter, 1.0f);
    m_params.spacing  = std::max(params.spacing, 0.01f);
    const RGBA c = params.color;
    m_color = canonical({ mul255(c.r, c.a), mul255(c.g, c.a), mul255(c.b, c.a), c.a });   // premultiply
    buildDabTable();
    m_active = true;
    m_lastX = x;
    m_lastY = y;
    m_sinceDab = 0;
    stampDab(x, y);
}

void BrushStroke::moveTo(float x, float y)
{
    if (!m_active) return;
    const float dx = x - m_lastX, dy = y - m_lastY;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist <= 0.0f) return;

    // Dabs every `step` pixels along the path; what's left over carries into the next segment,
    // so spacing stays even however the mouse samples arrive. Never more than half the
    // diameter apart, so a stroke has no gaps whatever the spacing setting.
    const float step = std::min(m_params.spacing, 0.5f) * m_params.diameter;
    const float ux = dx / dist, uy = dy / dist;
    float travelled = 0.0f;
    float need = step - m_sinceDab;
    while (travelled + need <= dist) {
        travelled += need;
        stampDab(m_lastX + ux * travelled, m_lastY + uy * travelled);
        need = step;
    }
    m_sinceDab = (step - need) + (dist - travelled);
    m_lastX = x;
    m_lastY = y;
}

void BrushStroke::end()
{
    m_scratch.clear();
    m_dirty.clear();
    m_active = false;
}

float BrushStroke::coverage(float d) const
{
    const float r = m_params.diameter * 0.5f;
    if (!m_params.antialias)
        return d <= std::max(r, 0.5f) ? 1.0f : 0.0f;
    const float inner   = r * std::clamp(m_params.hardness, 0.0f, 1.0f);
    const float soft    = r - inner;   // 0 at hardness 1: then only the rim softens the edge
    const float falloff = d <= inner || soft < 1e-3f ? 1.0f : 1.0f - smoothstep((d - inner) / soft);
    const float edge    = std::clamp(r + 0.5f - d, 0.0f, 1.0f);   // 1 px antialiased rim
    return falloff * edge;
}

void BrushStroke::buildDabTable()
{
    // Coverage (with flow) by squared distance from the dab centre, interpolated: no sqrt or
    // smoothstep per pixel.
    m_dabRadius = m_params.diameter * 0.5f + 0.5f;   // + half a pixel: the antialiased rim
    const int flow = std::clamp(int(m_params.flow * 255.0f + 0.5f), 0, 255);
    const float r2 = m_dabRadius * m_dabRadius;
    m_dabToIndex = float(kDabTable - 1) / r2;
    m_flowK  = uint8_t(flow);
    m_hardR2 = std::max(m_params.diameter * 0.5f, 0.5f) * std::max(m_params.diameter * 0.5f, 0.5f);
    for (int i = 0; i < kDabTable; ++i) {
        const float c = coverage(std::sqrt(float(i) / m_dabToIndex));
        m_dabTable[i] = c * 255.0f * float(flow) / 255.0f;
    }
    m_dabTable[kDabTable] = 0.0f;   // so interpolating at the very edge reads past nothing
}

void BrushStroke::stampDab(float cx, float cy)
{
    const float r = m_dabRadius;
    const BoundsI area{ clampWorld(cx - r), clampWorld(cy - r), clampWorld(cx + r), clampWorld(cy + r) };
    if (m_params.flow <= 0.0f) return;
    const float r2 = r * r;

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
                        const float f = d2 * m_dabToIndex;
                        const int   i = int(f);
                        k = int(m_dabTable[i] + (m_dabTable[i + 1] - m_dabTable[i]) * (f - float(i)) + 0.5f);
                    } else {
                        k = d2 <= m_hardR2 ? m_flowK : 0;   // a hard edge: exact
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

void applyStroke(RasterEditor& ed, const BrushStroke& stroke, const TileSet& tiles,
                 const std::function<const Tile*(int tx, int ty)>& original)
{
    auto composed = std::make_unique<Tile>();
    for (TileKey key : tiles) {
        const auto [tx, ty] = TileUtils::unpackKey(key);
        if (stroke.composeTile(*composed, original(tx, ty), tx, ty))
            ed.applyTile(tx, ty, *composed, { 255, CompositeMode::Replace });
        else
            ed.eraseTile(tx, ty);   // erased down to nothing
    }
}
