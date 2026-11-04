#include "base/bytes.hpp"

#include <bit>

void ByteWriter::u16(uint16_t v)
{
    u8(uint8_t(v));
    u8(uint8_t(v >> 8));
}

void ByteWriter::u32(uint32_t v)
{
    u16(uint16_t(v));
    u16(uint16_t(v >> 16));
}

void ByteWriter::u64(uint64_t v)
{
    u32(uint32_t(v));
    u32(uint32_t(v >> 32));
}

void ByteWriter::f32(float v)
{
    u32(std::bit_cast<uint32_t>(v));
}

void ByteWriter::text(const std::string& s)
{
    u32(uint32_t(s.size()));
    raw({ reinterpret_cast<const uint8_t*>(s.data()), s.size() });
}

size_t ByteWriter::reserveU64()
{
    const size_t at = m_bytes.size();
    u64(0);
    return at;
}

void ByteWriter::patchU64(size_t at, uint64_t v)
{
    for (int i = 0; i < 8; ++i) m_bytes[at + size_t(i)] = uint8_t(v >> (8 * i));
}

bool ByteReader::fits(size_t n)
{
    if (m_ok && n <= m_bytes.size() - m_at) return true;
    m_ok = false;
    return false;
}

uint8_t ByteReader::u8()
{
    return fits(1) ? m_bytes[m_at++] : 0;
}

uint16_t ByteReader::u16()
{
    const uint16_t lo = u8();
    return uint16_t(lo | uint16_t(u8()) << 8);
}

uint32_t ByteReader::u32()
{
    const uint32_t lo = u16();
    return lo | uint32_t(u16()) << 16;
}

uint64_t ByteReader::u64()
{
    const uint64_t lo = u32();
    return lo | uint64_t(u32()) << 32;
}

float ByteReader::f32()
{
    return std::bit_cast<float>(u32());
}

std::string ByteReader::text(size_t maxLength)
{
    const uint32_t n = u32();
    if (n > maxLength) {
        m_ok = false;
        return {};
    }
    const std::span<const uint8_t> b = raw(n);
    return std::string(b.begin(), b.end());
}

std::span<const uint8_t> ByteReader::raw(size_t n)
{
    if (!fits(n)) return {};
    const std::span<const uint8_t> out = m_bytes.subspan(m_at, n);
    m_at += n;
    return out;
}

ByteReader ByteReader::sub(size_t n)
{
    ByteReader r(raw(n));
    if (!m_ok) r.m_ok = false;
    return r;
}
