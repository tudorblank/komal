#pragma once
// Binary data written and read the same on every platform: little-endian, floats as their bits.
// The one way the app writes its own binary files (a .komal document).
//
//   ByteWriter w;
//   w.u32(7); w.f32(0.5f); w.text("Raster 1"); w.raw(bytes);
//   ByteReader r(w.bytes());
//   const uint32_t n = r.u32(); ...
//   if (!r.ok()) ...   // it ran past the end (or a length didn't fit): every read since gave 0
//
// A reader never reads outside its bytes: once a read doesn't fit it fails for good, so a
// truncated or damaged file is noticed once, at the end, instead of at every read.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

class ByteWriter {
public:
    void u8(uint8_t v)   { m_bytes.push_back(v); }
    void u16(uint16_t v);
    void u32(uint32_t v);
    void u64(uint64_t v);
    void i32(int32_t v)  { u32(uint32_t(v)); }
    void f32(float v);
    void text(const std::string& s);   // its length (u32), then its bytes (UTF-8)
    void raw(std::span<const uint8_t> bytes) { m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end()); }

    // A length written now, filled in later: what's written in between (a chunk's size).
    size_t reserveU64();
    void   patchU64(size_t at, uint64_t v);

    size_t size() const { return m_bytes.size(); }
    const std::vector<uint8_t>& bytes() const { return m_bytes; }
    std::vector<uint8_t> take() { return std::move(m_bytes); }

private:
    std::vector<uint8_t> m_bytes;
};

class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> bytes) : m_bytes(bytes) {}

    uint8_t  u8();
    uint16_t u16();
    uint32_t u32();
    uint64_t u64();
    int32_t  i32() { return int32_t(u32()); }
    float    f32();
    std::string text(size_t maxLength = 1 << 16);    // longer than that: failed
    std::span<const uint8_t> raw(size_t n);           // a view of the next n bytes (empty if they aren't there)
    ByteReader sub(size_t n);                         // the next n bytes, read on their own (a chunk)
    void skip(size_t n) { raw(n); }

    bool   ok() const { return m_ok; }
    void   fail() { m_ok = false; }                   // the data read doesn't make sense: failed too
    size_t left() const { return m_ok ? m_bytes.size() - m_at : 0; }
    bool   atEnd() const { return left() == 0; }

private:
    bool fits(size_t n);

    std::span<const uint8_t> m_bytes;
    size_t m_at = 0;
    bool   m_ok = true;
};
