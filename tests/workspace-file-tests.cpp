// A workspace saved as a .komal file and opened again: the same workspace, to the byte (its pixels
// as the raster holds them, never an image format), nothing to undo; a file that isn't one, or
// doesn't hold together, refused with why and never half-opened.

#include "helpers.hpp"
#include "base/bytes.hpp"
#include "base/file.hpp"
#include "document/documents.hpp"
#include "document/documents-actions.hpp"
#include "raster/tile-codec.hpp"

#include <cstdio>
#include <filesystem>

namespace {

std::string scratchFile(const char* name)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "komal-file-tests";
    std::filesystem::create_directories(dir);
    const std::u8string s = (dir / name).u8string();
    return std::string(s.begin(), s.end());
}

// A workspace with a bit of everything a file keeps: rasters, a move with an offset, a mask link,
// a mute, an artboard holding a layer, scales, names, a moved view.
void furnish(Workspace& p, NodeId paint, NodeId background)
{
    const NodeId master = p.master().id();
    const NodeId move = p.addNode(NodeType::Move, 500.0f, 80.0f, { .from = paint, .into = kNoNodeId, .slot = {} });
    REQUIRE(p.setNodeSettings(move, MoveSettings{ 12, -7 }));
    REQUIRE(p.connect(move, master, InputSlot::newLayer(uint32_t(p.master().layers().size()))));
    REQUIRE(p.connect(background, move, InputSlot::mask(NodeSignal::Main, NodeChannel::A)));
    REQUIRE(p.setSignalScale(move, NodeSignal::Main, NodeChannel::R, 0.25f));
    REQUIRE(p.setNodeLabel(paint, "Disc"));
    REQUIRE(p.setLayerMuted(master, background, true));
    const ArtboardId board = p.addArtboard({ 0, 0, 199, 149 });
    REQUIRE(board != kNoArtboard);
    REQUIRE(p.moveStackRows(master, { { paint }, {}, board, 0 }));
    Artboard a = *p.artboard(board);
    a.color = Color8{ 20, 200, 120, 255 };
    a.name = "Cover";
    REQUIRE(p.setArtboard(a));
    CanvasCamera cam = p.canvasCamera();
    cam.panBy({ 30.0f, -12.0f });
    cam.zoomAt({ 100.0f, 100.0f }, 2.0f);
    REQUIRE(p.setCanvasCamera(cam));
}

// The same file as version 1 wrote it: its tiles raw (count, then tx, ty, 16 KB each).
std::vector<uint8_t> asVersion1(const std::vector<uint8_t>& v2)
{
    ByteReader r(v2);
    ByteWriter w;
    w.raw(r.raw(8));   // the magic
    r.u32();
    w.u32(1);
    while (!r.atEnd()) {
        const uint32_t tag = r.u32();
        const uint64_t size = r.u64();
        ByteReader c = r.sub(size_t(size));
        w.u32(tag);
        const size_t at = w.reserveU64();
        const size_t start = w.size();
        constexpr uint32_t kTile = uint32_t('T') | uint32_t('I') << 8 | uint32_t('L') << 16 | uint32_t('E') << 24;
        if (tag == kTile) {
            w.u32(c.u32());   // its node
            TileGrid tiles;
            REQUIRE(decodeTiles(c, tiles));
            w.u32(uint32_t(tiles.size()));
            tiles.forEachRead([&](int tx, int ty, const Tile& t) {
                w.i32(tx);
                w.i32(ty);
                w.raw({ reinterpret_cast<const uint8_t*>(t.getReadAll()), sizeof(RGBA) * Tile::kPixels });
            });
        } else {
            w.raw(c.raw(size_t(size)));
        }
        w.patchU64(at, w.size() - start);
    }
    return w.take();
}

}

TEST_CASE("bytes: little-endian on every platform, a short read fails once and for all")
{
    ByteWriter w;
    w.u32(0x01020304u);
    w.f32(0.5f);
    w.text("Raster ü");
    CHECK(w.bytes()[0] == 0x04);   // little-endian
    ByteReader r(w.bytes());
    CHECK(r.u32() == 0x01020304u);
    CHECK(r.f32() == 0.5f);
    CHECK(r.text() == "Raster ü");
    CHECK(r.ok());
    CHECK(r.atEnd());
    CHECK(r.u32() == 0);           // past the end: nothing, and failed from now on
    CHECK_FALSE(r.ok());

    ByteWriter huge;
    huge.u32(1000000);             // a length claiming more than there is
    ByteReader h(huge.bytes());
    CHECK(h.text().empty());
    CHECK_FALSE(h.ok());
}

TEST_CASE("a workspace saved and opened: the same, to the byte, with nothing to undo")
{
    StarterWorkspace s;
    furnish(s.workspace, s.paint, s.background);
    const std::vector<uint8_t> bytes = encodeWorkspace(s.workspace.saved());

    std::string why;
    const std::optional<WorkspaceFile> file = decodeWorkspace(bytes, why);
    REQUIRE_MESSAGE(file, why);
    Workspace opened;
    REQUIRE_MESSAGE(opened.open(*file, why), why);

    CHECK(encodeWorkspace(opened.saved()) == bytes);   // everything it keeps, kept
    CHECK(opened.history().size() == 1);                // nothing to undo
    CHECK(opened.artboardsMatchStack());
    CHECK(opened.find(s.paint)->label() == "Disc");
    CHECK(opened.activeRasterId() == s.workspace.activeRasterId());
    CHECK(opened.canvasCamera() == s.workspace.canvasCamera());
    // The picture: every pixel of every raster, and the master as it composites them.
    for (const NodeId raster : { s.paint, s.background })
        for (const auto [x, y] : { std::pair{ 0, 0 }, { 50, 50 }, { -48, -48 }, { 310, 9 }, { 255, 191 } })
            CHECK(pixelOf(opened, raster, x, y) == pixelOf(s.workspace, raster, x, y));
    for (const auto [x, y] : { std::pair{ 10, 10 }, { 150, 120 }, { 320, 20 }, { -40, -40 } })
        CHECK(rendered(opened, opened.master().id(), x, y) == rendered(s.workspace, s.workspace.master().id(), x, y));

    // New nodes and artboards go on from where it was: no id, no name comes back.
    const NodeId next = opened.addNode(NodeType::Raster, 0.0f, 0.0f, {});
    CHECK(uint32_t(next) == file->nextNode);
    CHECK(opened.find(next)->label() == "Raster " + std::to_string(file->numbered + 1));
}

TEST_CASE("pixels are the tiles' own bytes: what's saved is what's read")
{
    Workspace p;
    NodeId raster = kNoNodeId;
    p.init([&](Workspace& w) {
        TileGrid g;
        for (int i = 0; i < 64; ++i) fillPixel(g, i, i, RGBA{ uint8_t(i), uint8_t(i * 2), uint8_t(i * 3), uint8_t(200) });
        fillPixel(g, -1, -1, RGBA{ 1, 2, 3, 4 });   // a tile of its own, on the other side of 0
        raster = w.importTiles(g, "", {});
    });
    std::string why;
    const std::optional<WorkspaceFile> file = decodeWorkspace(encodeWorkspace(p.saved()), why);
    REQUIRE(file);
    REQUIRE(file->rasters.size() == 1);
    CHECK(file->rasters[0].tiles.size() == 2);
    Workspace opened;
    REQUIRE(opened.open(*file, why));
    for (int i = 0; i < 64; ++i) CHECK(pixelOf(opened, raster, i, i) == (RGBA{ uint8_t(i), uint8_t(i * 2), uint8_t(i * 3), 200 }));
    CHECK(pixelOf(opened, raster, -1, -1) == RGBA{ 1, 2, 3, 4 });
    CHECK(pixelOf(opened, raster, 5, 6).a == 0);
}

TEST_CASE("a file that isn't one, is cut short, or comes from a newer Komal is refused with why")
{
    StarterWorkspace s;
    furnish(s.workspace, s.paint, s.background);
    const std::vector<uint8_t> bytes = encodeWorkspace(s.workspace.saved());
    std::string why;
    CHECK_FALSE(decodeWorkspace(std::vector<uint8_t>{ 'P', 'N', 'G' }, why));
    CHECK(why == "not a Komal project");

    std::vector<uint8_t> newer = bytes;
    newer[8] = uint8_t(WorkspaceFile::kVersion + 1);
    CHECK_FALSE(decodeWorkspace(newer, why));
    CHECK(why == "made by a newer Komal");

    // Cut anywhere: refused, never read past its end.
    for (size_t n = 0; n < bytes.size(); n += 1 + n / 7) {
        CAPTURE(n);
        CHECK_FALSE(decodeWorkspace(std::span(bytes).first(n), why));
    }

    // A chunk it doesn't know (a newer writer's, same version): skipped.
    std::vector<uint8_t> extra = bytes;
    const uint8_t chunk[] = { 'X', 'T', 'R', 'A', 3, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7 };
    extra.insert(extra.end(), std::begin(chunk), std::end(chunk));
    const std::optional<WorkspaceFile> withExtra = decodeWorkspace(extra, why);
    REQUIRE(withExtra);
    Workspace opened;
    REQUIRE(opened.open(*withExtra, why));
    CHECK(encodeWorkspace(opened.saved()) == bytes);
}

TEST_CASE("a file that doesn't hold together is refused: a link to nothing, a cycle, artboards astray")
{
    StarterWorkspace s;
    std::string why;

    WorkspaceFile toNothing = s.workspace.saved();
    toNothing.nodes.back().input = NodeId{ 999 };
    CHECK_FALSE(decodeWorkspace(encodeWorkspace(toNothing), why));
    CHECK(why == "damaged: a link");

    // Two moves feeding each other: each id is there, but connect() never allows the loop.
    WorkspaceFile loop = s.workspace.saved();
    const NodeId a{ loop.nextNode }, b{ loop.nextNode + 1 };
    loop.nextNode += 2;
    WorkspaceFile::Node m;
    m.type = NodeType::Move;
    m.settings = MoveSettings{};
    for (auto& row : m.scale) for (float& v : row) v = 1.0f;
    m.id = a; m.input = b; m.label = "A";
    loop.nodes.push_back(m);
    m.id = b; m.input = a; m.label = "B";
    loop.nodes.push_back(m);
    const std::optional<WorkspaceFile> decoded = decodeWorkspace(encodeWorkspace(loop), why);
    REQUIRE(decoded);   // the bytes are fine ...
    Workspace opened;
    CHECK_FALSE(opened.open(*decoded, why));   // ... the graph isn't
    CHECK(why == "damaged: a link that can't be");

    WorkspaceFile astray = s.workspace.saved();
    astray.lastArtboard = ArtboardId{ 1 };
    astray.artboards.push_back({ ArtboardId{ 1 }, "Lost", { 0, 0, 9, 9 }, {}, true });   // in no group of the master
    const std::optional<WorkspaceFile> lost = decodeWorkspace(encodeWorkspace(astray), why);
    REQUIRE(lost);
    Workspace stray;
    CHECK_FALSE(stray.open(*lost, why));
    CHECK(why == "damaged: artboards and the master's stack disagree");
}

TEST_CASE("documents save to their file and open from one: named after it, saved, nothing to undo")
{
    const std::string path = scratchFile("round trip ü.komal");
    std::filesystem::remove(std::filesystem::path(std::u8string(path.begin(), path.end())));
    Documents docs;
    ActionRegistry actions;
    registerDocumentsActions(actions, docs);

    Document& doc = docs.create();
    REQUIRE(doc.actions().run(Actions::NodeAdd, { { DocArgs::kType, "raster" }, { DocArgs::kX, 0.0f }, { DocArgs::kY, 0.0f } }));
    CHECK(doc.unsaved());
    const ActionResult noFile = actions.run(Actions::DocSave);
    CHECK_FALSE(noFile);
    CHECK(noFile.why == "no file to save to");

    const std::string without = path.substr(0, path.size() - 6);   // the extension: added
    REQUIRE(actions.run(Actions::DocSave, { { DocArgs::kPath, without } }));
    CHECK(doc.path() == path);
    CHECK(doc.name() == "round trip ü");
    CHECK_FALSE(doc.unsaved());

    // Open in another set of documents (another app): the same, saved, nothing to undo.
    Documents other;
    ActionRegistry otherActions;
    registerDocumentsActions(otherActions, other);
    const ActionResult opened = otherActions.run(Actions::DocOpen, { { DocArgs::kPath, path } });
    REQUIRE_MESSAGE(opened, opened.why);
    CHECK(other.size() == 1);
    CHECK(other.active().name() == "round trip ü");
    CHECK_FALSE(other.active().unsaved());
    CHECK(other.active().workspace().history().size() == 1);
    CHECK(other.active().workspace().nodes().size() == 2);
    // Opened again: that one, not a second.
    CHECK(otherActions.run(Actions::DocOpen, { { DocArgs::kPath, path } }).made == opened.made);
    CHECK(other.size() == 1);

    const ActionResult missing = otherActions.run(Actions::DocOpen, { { DocArgs::kPath, scratchFile("nothing here.komal") } });
    CHECK_FALSE(missing);
    CHECK_FALSE(missing.why.empty());
    CHECK(other.size() == 1);
}

TEST_CASE("tiles are compressed (raster/tile-codec); a version 1 file, raw tiles, still opens")
{
    StarterWorkspace s;
    furnish(s.workspace, s.paint, s.background);
    const std::vector<uint8_t> v2 = encodeWorkspace(s.workspace.saved());
    const std::vector<uint8_t> v1 = asVersion1(v2);
    std::printf(".komal of the starter workspace: %zu bytes raw (version 1), %zu compressed\n", v1.size(), v2.size());
    CHECK(v2.size() * 4 < v1.size());

    std::string why;
    const std::optional<WorkspaceFile> old = decodeWorkspace(v1, why);
    REQUIRE_MESSAGE(old, why);
    Workspace opened;
    REQUIRE_MESSAGE(opened.open(*old, why), why);
    CHECK(encodeWorkspace(opened.saved()) == v2);   // the same workspace: saved again, as version 2
}
