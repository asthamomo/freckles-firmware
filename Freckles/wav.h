#pragma once
#include <Arduino.h>

namespace wav {

constexpr size_t HEADER_SIZE = 44;

inline void put32(uint8_t* p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}
inline void put16(uint8_t* p, uint16_t v)
{
    p[0] = v;
    p[1] = v >> 8;
}
inline uint32_t get32(const uint8_t* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline uint16_t get16(const uint8_t* p)
{
    return p[0] | (p[1] << 8);
}

// Canonical 44-byte header for 16-bit mono PCM.
inline void writeHeader(uint8_t* h, size_t samples, uint32_t rate)
{
    uint32_t dataBytes = samples * 2;
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + dataBytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);  // PCM
    put16(h + 22, 1);  // mono
    put32(h + 24, rate);
    put32(h + 28, rate * 2);
    put16(h + 32, 2);
    put16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put32(h + 40, dataBytes);
}

struct Pcm {
    const int16_t* samples = nullptr;
    size_t count           = 0;
    uint32_t rate          = 0;
};

// Finds the PCM data in a WAV file. Accepts 16-bit mono PCM only.
inline bool parse(const uint8_t* buf, size_t len, Pcm& out)
{
    if (len < 12 || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) return false;
    size_t pos      = 12;
    bool fmtOk      = false;
    uint32_t rate   = 0;
    while (pos + 8 <= len) {
        const uint8_t* ck = buf + pos;
        uint32_t size     = get32(ck + 4);
        if (!memcmp(ck, "fmt ", 4) && size >= 16 && pos + 8 + 16 <= len) {
            uint16_t format = get16(ck + 8), ch = get16(ck + 10), bits = get16(ck + 22);
            rate  = get32(ck + 12);
            fmtOk = (format == 1 || format == 0xFFFE) && ch == 1 && bits == 16;
        } else if (!memcmp(ck, "data", 4)) {
            if (!fmtOk) return false;
            size_t avail = len - (pos + 8);
            // streamed WAVs often put 0 or 0xFFFFFFFF here
            size_t bytes = (size == 0 || size > avail) ? avail : size;
            out.samples  = reinterpret_cast<const int16_t*>(ck + 8);
            out.count    = bytes / 2;
            out.rate     = rate;
            return out.count > 0 && rate >= 8000 && rate <= 48000;
        }
        pos += 8 + size + (size & 1);
    }
    return false;
}

}  // namespace wav
