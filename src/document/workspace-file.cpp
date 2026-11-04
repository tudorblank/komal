#include "document/workspace-file.hpp"
#include "base/bytes.hpp"
#include "node/node-kinds.hpp"
#include "raster/tile-codec.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace {

constexpr uint8_t kMagic[8] = { 'K', 'O', 'M', 'A', 'L', 0x00, 0x0D, 0x0A };
constexpr size_t  kTileBytes = sizeof(RGBA) * Tile::kPixels;
constexpr size_t  kMaxText = 4096;   // a name

constexpr uint32_t tag(const char (&s)[5]) { return uint32_t(s[0]) | uint32_t(s[1]) << 8 | uint32_t(s[2]) << 16 | uint32_t(s[3]) << 24; }
constexpr uint32_t kWorkspace = tag("WKSP"), kNode = tag("NODE"), kArtboards = tag("ARTB"), kTiles = tag("TILE");

// A node's settings: a tag, then its kind's fields. A kind with new settings adds a tag here.
enum class SettingsTag : uint8_t { None = 0, Raster = 1, Move = 2, Compositor = 3 };

template <class... F> struct Overloaded : F... { using F::operator()...; };

void writeSettings(ByteWriter& w, const NodeSettings& s)
{
    std::visit(Overloaded{
        [&](const NoSettings&) { w.u8(uint8_t(SettingsTag::None)); },
        [&](const RasterSettings& r) {
            w.u8(uint8_t(SettingsTag::Raster));
            w.u8(r.locked);
            w.u8(r.alphaLocked);
        },
        [&](const MoveSettings& m) {
            w.u8(uint8_t(SettingsTag::Move));
            w.i32(m.x);
            w.i32(m.y);
        },
        [&](const CompositorSettings& c) {
            w.u8(uint8_t(SettingsTag::Compositor));
            w.u32(uint32_t(c.muted.size()));
            for (NodeId id : c.muted) w.u32(uint32_t(id));
            w.u32(uint32_t(c.groups.size()));
            for (const StackGroup& g : c.groups) {
                w.u32(uint32_t(g.key));
                w.u32(g.begin);
                w.u32(g.end);
            }
        },
    }, s);
}

std::optional<NodeSettings> readSettings(ByteReader& r)
{
    switch (SettingsTag(r.u8())) {
    case SettingsTag::None: return NoSettings{};
    case SettingsTag::Raster: {
        RasterSettings s;
        s.locked = r.u8() != 0;
        s.alphaLocked = r.u8() != 0;
        return s;
    }
    case SettingsTag::Move: {
        MoveSettings s;
        s.x = r.i32();
        s.y = r.i32();
        if (!TileUtils::inWorld(s.x) || !TileUtils::inWorld(s.y)) return std::nullopt;
        return s;
    }
    case SettingsTag::Compositor: {
        CompositorSettings s;
        const uint32_t muted = r.u32();
        if (muted > r.left() / 4) return std::nullopt;
        for (uint32_t i = 0; i < muted; ++i) s.muted.push_back(NodeId(r.u32()));
        const uint32_t groups = r.u32();
        if (groups > r.left() / 12) return std::nullopt;
        for (uint32_t i = 0; i < groups; ++i) {
            StackGroup g;
            g.key = GroupKey(r.u32());
            g.begin = r.u32();
            g.end = r.u32();
            s.groups.push_back(g);
        }
        return s;
    }
    }
    return std::nullopt;
}

// A chunk: its tag and size, then what `body` writes.
template <class F> void chunk(ByteWriter& w, uint32_t t, F&& body)
{
    w.u32(t);
    const size_t size = w.reserveU64();
    const size_t start = w.size();
    body();
    w.patchU64(size, uint64_t(w.size() - start));
}

bool finite(float v) { return std::isfinite(v); }

}

std::vector<uint8_t> encodeWorkspace(const WorkspaceFile& f)
{
    ByteWriter w;
    w.raw(kMagic);
    w.u32(WorkspaceFile::kVersion);
    chunk(w, kWorkspace, [&] {
        w.u32(f.nextNode);
        w.u32(f.numbered);
        w.u32(uint32_t(f.lastArtboard));
        w.u32(uint32_t(f.master));
        w.u32(uint32_t(f.active));
        w.f32(f.camera.pan.x);
        w.f32(f.camera.pan.y);
        w.f32(f.camera.zoom);
        w.f32(f.camera.rotation);
    });
    for (const WorkspaceFile::Node& n : f.nodes)
        chunk(w, kNode, [&] {
            w.u32(uint32_t(n.id));
            w.text(nodeKind(n.type).id);
            w.text(n.label);
            w.f32(n.x);
            w.f32(n.y);
            for (int s = 0; s < kSignalCount; ++s)
                for (int c = 0; c < kChannelCount; ++c) w.f32(n.scale[s][c]);
            for (int s = 0; s < kSignalCount; ++s)
                for (int c = 0; c < kChannelCount; ++c) w.u32(uint32_t(n.mask[s][c]));
            w.u32(uint32_t(n.input));
            w.u32(uint32_t(n.layers.size()));
            for (NodeId id : n.layers) w.u32(uint32_t(id));
            writeSettings(w, n.settings);
        });
    chunk(w, kArtboards, [&] {
        w.u32(uint32_t(f.artboards.size()));
        for (const Artboard& a : f.artboards) {
            w.u32(uint32_t(a.id));
            w.text(a.name);
            w.i32(a.rect.minX);
            w.i32(a.rect.minY);
            w.i32(a.rect.maxX);
            w.i32(a.rect.maxY);
            w.u8(a.color.r);
            w.u8(a.color.g);
            w.u8(a.color.b);
            w.u8(a.color.a);
            w.u8(a.visible);
        }
    });
    for (const WorkspaceFile::Raster& r : f.rasters)
        chunk(w, kTiles, [&] {
            w.u32(uint32_t(r.node));
            encodeTiles(r.tiles, w);
        });
    return w.take();
}

std::optional<WorkspaceFile> decodeWorkspace(std::span<const uint8_t> bytes, std::string& error)
{
    const auto refuse = [&](const char* why) -> std::optional<WorkspaceFile> {
        error = why;
        return std::nullopt;
    };
    ByteReader r(bytes);
    const std::span<const uint8_t> magic = r.raw(sizeof kMagic);
    if (!r.ok() || std::memcmp(magic.data(), kMagic, sizeof kMagic) != 0) return refuse("not a Komal project");
    const uint32_t version = r.u32();
    if (!r.ok() || version == 0) return refuse("not a Komal project");
    if (version > WorkspaceFile::kVersion) return refuse("made by a newer Komal");

    WorkspaceFile f;
    bool sawWorkspace = false, sawArtboards = false;
    absl::flat_hash_map<NodeId, NodeType> types;
    absl::flat_hash_set<NodeId> withTiles;
    while (!r.atEnd()) {
        const uint32_t t = r.u32();
        const uint64_t size = r.u64();
        if (!r.ok() || size > r.left()) return refuse("cut short");
        ByteReader c = r.sub(size_t(size));
        if (t == kWorkspace) {
            if (sawWorkspace) return refuse("damaged: two workspaces");
            sawWorkspace = true;
            f.nextNode = c.u32();
            f.numbered = c.u32();
            f.lastArtboard = ArtboardId(c.u32());
            f.master = NodeId(c.u32());
            f.active = NodeId(c.u32());
            f.camera.pan = { c.f32(), c.f32() };
            f.camera.zoom = c.f32();
            const float rotation = c.f32();
            if (!finite(f.camera.pan.x) || !finite(f.camera.pan.y) || !finite(f.camera.zoom) || !finite(rotation))
                return refuse("damaged: the view");
            f.camera.zoom = std::clamp(f.camera.zoom, CanvasCamera::kMinZoom, CanvasCamera::kMaxZoom);
            f.camera.rotation = CanvasCamera::wrapAngle(rotation);
        } else if (t == kNode) {
            WorkspaceFile::Node n;
            n.id = NodeId(c.u32());
            const NodeKind* kind = findNodeKind(c.text(kMaxText));
            n.label = c.text(kMaxText);
            n.x = c.f32();
            n.y = c.f32();
            for (int s = 0; s < kSignalCount; ++s)
                for (int ch = 0; ch < kChannelCount; ++ch) n.scale[s][ch] = c.f32();
            for (int s = 0; s < kSignalCount; ++s)
                for (int ch = 0; ch < kChannelCount; ++ch) n.mask[s][ch] = NodeId(c.u32());
            n.input = NodeId(c.u32());
            const uint32_t layers = c.u32();
            if (layers > c.left() / 4) return refuse("damaged: a stack");
            for (uint32_t i = 0; i < layers; ++i) n.layers.push_back(NodeId(c.u32()));
            const std::optional<NodeSettings> settings = readSettings(c);
            if (!c.ok() || !settings) return refuse("damaged: a node");
            if (!kind) return refuse("a kind of node this Komal doesn't know");
            if (n.id == kNoNodeId || (!f.nodes.empty() && n.id <= f.nodes.back().id)) return refuse("damaged: node ids");
            if (!finite(n.x) || !finite(n.y)) return refuse("damaged: a node's place");
            for (const auto& row : n.scale)
                for (float v : row)
                    if (!finite(v) || v < 0.0f || v > 1.0f) return refuse("damaged: a node's signal");
            n.type = kind->type;
            n.settings = *settings;
            types.emplace(n.id, n.type);
            f.nodes.push_back(std::move(n));
        } else if (t == kArtboards) {
            if (sawArtboards) return refuse("damaged: two artboard lists");
            sawArtboards = true;
            const uint32_t count = c.u32();
            if (count > c.left() / 25) return refuse("damaged: artboards");
            for (uint32_t i = 0; i < count; ++i) {
                Artboard a;
                a.id = ArtboardId(c.u32());
                a.name = c.text(kMaxText);
                a.rect = { c.i32(), c.i32(), c.i32(), c.i32() };
                a.color = Color8{ c.u8(), c.u8(), c.u8(), c.u8() };
                a.visible = c.u8() != 0;
                f.artboards.push_back(std::move(a));
            }
        } else if (t == kTiles) {
            WorkspaceFile::Raster raster;
            raster.node = NodeId(c.u32());
            if (version >= 2) {
                if (!decodeTiles(c, raster.tiles)) return refuse("damaged: a raster's pixels");
                f.rasters.push_back(std::move(raster));
                continue;
            }
            const uint32_t count = c.u32();   // version 1: raw tiles
            if (!c.ok() || count > c.left() / (8 + kTileBytes)) return refuse("damaged: a raster's pixels");
            for (uint32_t i = 0; i < count; ++i) {
                const int tx = c.i32(), ty = c.i32();
                const std::span<const uint8_t> px = c.raw(kTileBytes);
                if (!c.ok()) return refuse("damaged: a raster's pixels");
                const int limit = TileUtils::kWorldLimit >> TileUtils::kTileShift;   // in tiles: nothing overflows
                if (tx < -limit || tx > limit || ty < -limit || ty > limit) return refuse("damaged: pixels out of the world");
                Tile& tile = raster.tiles.writeOrCreateTile(tx, ty);
                {
                    Tile::BulkWrite write(tile, Tile::BulkWrite::Untrusted);   // from outside: made canonical
                    std::memcpy(write.data(), px.data(), kTileBytes);
                    write.markChanged();
                }
                tile.settle();
                if (tile.isEmpty()) raster.tiles.freeTile(tx, ty);
            }
            f.rasters.push_back(std::move(raster));
        }
        // Anything else: a newer writer's, skipped.
        if (!c.ok()) return refuse("damaged");
    }
    if (!r.ok()) return refuse("cut short");
    if (!sawWorkspace) return refuse("damaged: no workspace");

    // It holds together: everything it names is in it.
    const auto is = [&](NodeId id, std::optional<NodeType> type = std::nullopt) {
        const auto it = types.find(id);
        return it != types.end() && (!type || it->second == *type);
    };
    if (!is(f.master, NodeType::Compositor)) return refuse("damaged: no master");
    if (f.active != kNoNodeId && !is(f.active, NodeType::Raster)) return refuse("damaged: the active raster");
    if (!f.nodes.empty() && f.nextNode <= uint32_t(f.nodes.back().id)) return refuse("damaged: node ids");
    for (const WorkspaceFile::Node& n : f.nodes) {
        if (n.input != kNoNodeId && !is(n.input)) return refuse("damaged: a link");
        for (NodeId id : n.layers)
            if (!is(id)) return refuse("damaged: a link");
        for (const auto& row : n.mask)
            for (NodeId id : row)
                if (id != kNoNodeId && !is(id)) return refuse("damaged: a link");
    }
    for (const WorkspaceFile::Raster& raster : f.rasters)
        if (!is(raster.node, NodeType::Raster) || !withTiles.insert(raster.node).second) return refuse("damaged: a raster's pixels");
    absl::flat_hash_set<ArtboardId> boards;
    for (const Artboard& a : f.artboards)
        if (a.id == kNoArtboard || uint32_t(a.id) > uint32_t(f.lastArtboard) || !boards.insert(a.id).second || a.name.empty())
            return refuse("damaged: artboards");
    return f;
}
