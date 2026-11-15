#include "base/file.hpp"

#include <algorithm>
#include <cctype>
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
        if (!out || !out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())) || !out.flush()) {
            error = "can't be written";
            return false;
        }
        return true;
    }

    bool write(const std::string& path, const std::string& text, std::string& error)
    {
        return write(path, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()), error);
    }

    bool replace(const std::string& path, std::span<const uint8_t> bytes, std::string& error)
    {
        const std::string partial = path + ".partial";
        std::error_code ec;
        if (!write(partial, bytes, error)) {
            std::filesystem::remove(pathOf(partial), ec);
            return false;
        }
        std::filesystem::rename(pathOf(partial), pathOf(path), ec);   // replaces it (on Windows too)
        if (ec) {
            error = ec.message();
            std::filesystem::remove(pathOf(partial), ec);
            return false;
        }
        return true;
    }

    bool replace(const std::string& path, const std::string& text, std::string& error)
    {
        return replace(path, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()), error);
    }

    std::string name(const std::string& path) { return utf8Of(pathOf(path).filename().u8string()); }
    std::string stem(const std::string& path) { return utf8Of(pathOf(path).stem().u8string()); }

    bool hasExtension(const std::string& path, std::string_view extension)
    {
        const std::string has = utf8Of(pathOf(path).extension().u8string());
        return std::equal(has.begin(), has.end(), extension.begin(), extension.end(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
    }

    std::vector<std::string> list(const std::string& folder, std::string_view extension)
    {
        std::vector<std::string> out;
        std::error_code ec;
        // Stepped with an error code, never thrown: a folder that goes bad halfway just ends the list.
        for (auto it = std::filesystem::directory_iterator(pathOf(folder), ec); !ec && it != std::filesystem::directory_iterator();
             it.increment(ec)) {
            std::error_code fileEc;
            if (!it->is_regular_file(fileEc)) continue;
            const std::string path = utf8Of(it->path().u8string());
            if (hasExtension(path, extension)) out.push_back(path);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

}
