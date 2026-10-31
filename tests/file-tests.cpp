#include "base/file.hpp"
#include "document/image-load.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <vector>

namespace {

// A folder of its own under the system's temp folder, with a name that isn't ASCII.
std::string scratchDir()
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / std::filesystem::path(u8"komal-tests-ünïcødé");
    std::filesystem::create_directories(dir);
    const std::u8string s = dir.u8string();
    return std::string(s.begin(), s.end());
}

// An uncompressed 32-bit TGA, top-left first, BGRA.
std::vector<uint8_t> tga(uint16_t w, uint16_t h, const std::vector<uint8_t>& bgra)
{
    std::vector<uint8_t> out = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                 uint8_t(w & 0xFF), uint8_t(w >> 8), uint8_t(h & 0xFF), uint8_t(h >> 8), 32, 0x28 };
    out.insert(out.end(), bgra.begin(), bgra.end());
    return out;
}

}

TEST_CASE("files: a path that isn't ASCII reads and writes, a missing one says why")
{
    const std::string path = scratchDir() + "/fïle.txt";
    std::string error;
    REQUIRE(File::write(path, std::string("hello"), error));
    std::vector<uint8_t> bytes;
    REQUIRE(File::read(path, bytes, error));
    CHECK(std::string(bytes.begin(), bytes.end()) == "hello");
    CHECK(File::name(path) == "fïle.txt");
    CHECK(File::stem(path) == "fïle");
    CHECK_FALSE(File::read(scratchDir() + "/none.txt", bytes, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("an image file decodes premultiplied; one too big is refused before decoding")
{
    std::string error;
    const std::string small = scratchDir() + "/ïmage.tga";
    // two pixels: opaque red, half-transparent white
    REQUIRE(File::write(small, tga(2, 1, { 0, 0, 255, 255,   255, 255, 255, 128 }), error));
    DecodedImage image;
    REQUIRE(loadImageFile(small, image, error));
    CHECK(image.width() == 2);
    CHECK(image.height() == 1);
    const FlatImage flat = image.flat();
    CHECK(flat.rgba[0] == 255);   // red, opaque
    CHECK(flat.rgba[3] == 255);
    CHECK(flat.rgba[4] == 128);   // white at half: premultiplied to half
    CHECK(flat.rgba[7] == 128);

    // A header claiming 20000 x 20000 (over 2^28 pixels): refused by its size, never decoded.
    const std::string big = scratchDir() + "/big.tga";
    REQUIRE(File::write(big, tga(20000, 20000, {}), error));
    DecodedImage none;
    CHECK_FALSE(loadImageFile(big, none, error));
    CHECK(error.find("too large") != std::string::npos);
}
