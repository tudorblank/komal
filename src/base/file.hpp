#pragma once
// Files by UTF-8 path, the same on every platform: a Windows path with any characters in it opens
// too (the standard library's wide-path streams, never fopen on narrow strings). The one way the
// app reads and writes files.
//
//   std::vector<uint8_t> bytes;
//   if (!File::read(path, bytes, error)) ...

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace File {

bool read(const std::string& path, std::vector<uint8_t>& out, std::string& error);
bool write(const std::string& path, std::span<const uint8_t> bytes, std::string& error);   // the whole file, replaced
bool write(const std::string& path, const std::string& text, std::string& error);
// The whole file, replaced only once the new bytes are all written (beside it, then renamed over
// it): a save cut short leaves the old file as it was.
bool replace(const std::string& path, std::span<const uint8_t> bytes, std::string& error);
bool replace(const std::string& path, const std::string& text, std::string& error);

std::string name(const std::string& path);   // "photo.png"
std::string stem(const std::string& path);   // "photo"

// Its extension is `extension` (".toml"), in any case: "a.TOML" too.
bool hasExtension(const std::string& path, std::string_view extension);

// The files in `folder` with that extension (hasExtension), as paths, sorted.
// None if the folder isn't there or can't be read.
std::vector<std::string> list(const std::string& folder, std::string_view extension);

}