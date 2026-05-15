#include "hts_wrapper.hpp"
#include "erans.hpp"
#include "types.hpp"

#include "htscodecs/rANS_static4x16.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

extern "C" {
unsigned char *rans_compress_O0_4x16(unsigned char *in, unsigned int in_size,
                                     unsigned char *out, unsigned int *out_size);
unsigned char *rans_uncompress_O0_4x16(unsigned char *in, unsigned int in_size,
                                       unsigned char *out, unsigned int out_sz);
}

static void append_varu32(std::string& out, u32 value) {
    while (value >= 0x80) {
        out.push_back(static_cast<char>(value | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<char>(value));
}

static u32 read_varu32(std::string_view& in) {
    u32 value = 0;
    unsigned shift = 0;
    for (size_t i = 0; i < in.size() && i < 5; i++) {
        u32 byte = static_cast<unsigned char>(in[i]);
        value |= (byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            in.remove_prefix(i + 1);
            return value;
        }
        shift += 7;
    }
    throw std::runtime_error("bad varint in hts block");
}

void hts_rans4x16_encode(std::string_view in, std::string& out) {
    if (in.size() > erans_maxsize)
        throw std::runtime_error("hts rans input block exceeds 16MiB");

    auto src = reinterpret_cast<unsigned char*>(const_cast<char*>(in.data()));
    unsigned int payload_size = 0;
    unsigned char *payload = rans_compress_O0_4x16(src, in.size(), nullptr, &payload_size);
    if (!payload && payload_size != 0)
        throw std::runtime_error("hts rans compression failed");

    out.clear();
    append_varu32(out, static_cast<u32>(in.size()));
    out.append(reinterpret_cast<const char*>(payload), payload_size);
    std::free(payload);
}

void hts_rans4x16_decode(std::string_view in, std::string& out) {
    u32 raw_size = read_varu32(in);
    if (raw_size > erans_maxsize)
        throw std::runtime_error("decoded hts rans block exceeds 16MiB");
    if (raw_size == 0) {
        if (!in.empty())
            throw std::runtime_error("non-empty hts rans payload for empty block");
        out.clear();
        return;
    }

    out.resize(raw_size);
    auto src = reinterpret_cast<unsigned char*>(const_cast<char*>(in.data()));
    auto dst = reinterpret_cast<unsigned char*>(out.data());
    if (!rans_uncompress_O0_4x16(src, in.size(), dst, raw_size))
        throw std::runtime_error("hts rans decompression failed");
}
