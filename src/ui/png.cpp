// SPDX-License-Identifier: Apache-2.0
#include "ui/png.h"

#include <cstdio>
#include <vector>

namespace ui {

namespace {

uint32_t crc_table[256];
bool crc_ready = false;

void crc_init()
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1u)) : (c >> 1u);
        }
        crc_table[n] = c;
    }
    crc_ready = true;
}

uint32_t crc32(const uint8_t *data, size_t size, uint32_t crc = 0xFFFFFFFFu)
{
    if (!crc_ready) {
        crc_init();
    }
    for (size_t i = 0; i < size; i++) {
        crc = crc_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8u);
    }
    return crc;
}

void put32(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v >> 24u));
    out.push_back(static_cast<uint8_t>(v >> 16u));
    out.push_back(static_cast<uint8_t>(v >> 8u));
    out.push_back(static_cast<uint8_t>(v));
}

void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data)
{
    put32(out, static_cast<uint32_t>(data.size()));
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    uint32_t crc = crc32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu;
    put32(out, crc);
}

// zlib stream with stored (uncompressed) deflate blocks of up to 65535 bytes.
std::vector<uint8_t> zlib_store(const std::vector<uint8_t> &raw)
{
    std::vector<uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t pos = 0;
    do {
        size_t len = raw.size() - pos;
        if (len > 65535) {
            len = 65535;
        }
        bool last = pos + len == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(len & 0xFFu));
        z.push_back(static_cast<uint8_t>(len >> 8u));
        z.push_back(static_cast<uint8_t>(~len & 0xFFu));
        z.push_back(static_cast<uint8_t>((~len >> 8u) & 0xFFu));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
        pos += len;
    } while (pos < raw.size());
    uint32_t a = 1;
    uint32_t b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    put32(z, (b << 16u) | a);
    return z;
}

}  // namespace

bool write_png(const std::string &path, int width, int height, const uint8_t *rgba)
{
    if (width <= 0 || height <= 0) {
        return false;
    }
    size_t w = static_cast<size_t>(width);
    size_t h = static_cast<size_t>(height);
    std::vector<uint8_t> raw;
    raw.reserve(h * (w * 4 + 1));
    for (size_t y = 0; y < h; y++) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), rgba + y * w * 4, rgba + (y + 1) * w * 4);
    }

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    put32(ihdr, static_cast<uint32_t>(width));
    put32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", zlib_store(raw));
    chunk(out, "IEND", {});

    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }
    bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

}  // namespace ui
