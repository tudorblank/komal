#include "project/workspace-file.hpp"
#include "base/overloaded.hpp"
#include "base/bytes.hpp"
#include "project/fits.hpp"
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

// A node's settings: a tag, then its kind's fields. A kind with new settings adds a tag here.
enum class SettingsTag : uint8_t { None = 0, Raster = 1, Move = 2, Compositor = 3 };
// Each tag is its NodeSettings alternative's index, and every alternative has one: new settings
// don't compile until they have a tag, and then readSettings' switch needs its case.
template <SettingsTag T, class S> constexpr bool kTagIs = std::is_same_v<std::variant_alternative_t<size_t(T), NodeSettings>, S>;
static_assert(kTagIs<SettingsTag::None, NoSettings> && kTagIs<SettingsTag::Raster, RasterSettings> &&
              kTagIs<SettingsTag::Move, MoveSettings> && kTagIs<SettingsTag::Compositor, CompositorSettings> &&
              std::variant_size_v<NodeSettings> == size_t(SettingsTag::Compositor) + 1, "a tag for every kind of settings");

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
        if (!fits(s)) return std::nullopt;
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

}

std::vector<uint8_t> encodeWorkspace(const WorkspaceFile& f)
{
    ByteWriter w;
    w.raw(kMagic);
    w.u32(WorkspaceFile::kVersion);
    chunk(w, WorkspaceFile::kWorkspaceChunk, [&] {
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
    for (const WorkspaceFile::SavedNode& saved : f.nodes)
        chunk(w, WorkspaceFile::kNodeChunk, [&] {
            const NodeRecord& n = saved.record;
            w.u32(uint32_t(saved.id));
            w.text(nodeKind(saved.type).id);
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
    chunk(w, WorkspaceFile::kArtboardsChunk, [&] {
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
        chunk(w, WorkspaceFile::kTilesChunk, [&] {
            w.u32(uint32_t(r.node));
            encodeTiles(r.tiles, w);
        });
    return w.take();
}

std::optional<WorkspaceFile> decodeWorkspace(std::span<const uint8_t> bytes, std::string& error)
{
    const auto refuse = [&](const char* message) -> std::optional<WorkspaceFile> {
        error = message;
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
        if (t == WorkspaceFile::kWorkspaceChunk) {
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
            if (!std::isfinite(f.camera.pan.x) || !std::isfinite(f.camera.pan.y) || !std::isfinite(f.camera.zoom) || !std::isfinite(rotation))
                return refuse("damaged: the view");
            f.camera.zoom = std::clamp(f.camera.zoom, CanvasCamera::kMinZoom, CanvasCamera::kMaxZoom);
            f.camera.rotation = CanvasCamera::wrapAngle(rotation);
        } else if (t == WorkspaceFile::kNodeChunk) {
            WorkspaceFile::SavedNode saved;
            NodeRecord& n = saved.record;
            saved.id = NodeId(c.u32());
            const NodeKind* kind = findNodeKind(c.text(kMaxName));
            n.label = c.text(kMaxName);
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
            if (saved.id == kNoNodeId || (!f.nodes.empty() && saved.id <= f.nodes.back().id)) return refuse("damaged: node ids");
            if (!labelFits(n.label)) return refuse("damaged: a node's name");
            if (!std::isfinite(n.x) || !std::isfinite(n.y)) return refuse("damaged: a node's place");
            for (const auto& row : n.scale)
                for (float v : row)
                    if (!scaleFits(v)) return refuse("damaged: a node's signal");
            saved.type = kind->type;
            n.settings = *settings;
            types.emplace(saved.id, saved.type);
            f.nodes.push_back(std::move(saved));
        } else if (t == WorkspaceFile::kArtboardsChunk) {
            if (sawArtboards) return refuse("damaged: two artboard lists");
            sawArtboards = true;
            const uint32_t count = c.u32();
            if (count > c.left() / 25) return refuse("damaged: artboards");
            for (uint32_t i = 0; i < count; ++i) {
                Artboard a;
                a.id = ArtboardId(c.u32());
                a.name = c.text(kMaxName);
                a.rect = { c.i32(), c.i32(), c.i32(), c.i32() };
                a.color = Color8{ c.u8(), c.u8(), c.u8(), c.u8() };
                a.visible = c.u8() != 0;
                f.artboards.push_back(std::move(a));
            }
        } else if (t == WorkspaceFile::kTilesChunk) {
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
                const int tx = c.i32(), ty = c.i32();   // the count fits what's left: these reads can't fail
                if (!TileUtils::tileInWorld(tx) || !TileUtils::tileInWorld(ty)) return refuse("damaged: pixels out of the world");
                Tile& tile = raster.tiles.writeOrCreateTile(tx, ty);
                if (!decodeRawTile(c, tile)) return refuse("damaged: a raster's pixels");
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
    for (const WorkspaceFile::SavedNode& saved : f.nodes) {
        const NodeRecord& n = saved.record;
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
        if (a.id == kNoArtboard || uint32_t(a.id) > uint32_t(f.lastArtboard) || !boards.insert(a.id).second || !fits(a))
            return refuse("damaged: artboards");
    return f;
}
