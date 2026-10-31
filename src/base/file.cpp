#include "base/file.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace {

std::filesystem::path pathOf(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string utf8Of(const std::u8string& s)
{
    return std::string(s.begin(), s.end());
}

}

namespace File {

bool read(const std::string& path, std::vector<uint8_t>& out, std::string& error)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(pathOf(path), ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    std::ifstream in(pathOf(path), std::ios::binary);
    if (!in) {
        error = "can't be opened";
        return false;
    }
    out.resize(size_t(size));
    if (!in.read(reinterpret_cast<char*>(out.data()), std::streamsize(size))) {
        error = "can't be read";
        return false;
    }
    return true;
}

bool write(const std::string& path, std::span<const uint8_t> bytes, std::string& error)
{
    std::ofstream out(pathOf(path), std::ios::binary | std::ios::trunc);
    if (!out || !out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()))) {
        error = "can't be written";
        return false;
    }
    return true;
}

bool write(const std::string& path, const std::string& text, std::string& error)
{
    return write(path, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()), error);
}

std::string name(const std::string& path) { return utf8Of(pathOf(path).filename().u8string()); }
std::string stem(const std::string& path) { return utf8Of(pathOf(path).stem().u8string()); }

}
