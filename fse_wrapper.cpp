#include "fse_wrapper.hpp"
#include "erans.hpp"

#include "fse.h"

#include <cstring>
#include <stdexcept>
#include <string>

static constexpr unsigned special_nibble = 0x0f;
static constexpr unsigned raw_block = 0;
static constexpr unsigned rle_block = 1;

// FSE normal payloads begin with writeNCount(); its low nibble is
// tableLog - FSE_MIN_TABLELOG and can never be 0xf.
static char special_header(unsigned block_type) {
    return static_cast<char>((block_type << 4) | special_nibble);
}

static bool is_special(char ch) {
    return (static_cast<unsigned char>(ch) & special_nibble) == special_nibble;
}

static u32 read24(const char *p) {
    auto b = reinterpret_cast<const unsigned char*>(p);
    return u32(b[0]) | (u32(b[1]) << 8) | (u32(b[2]) << 16);
}

static void write24(char *p, u32 value) {
    p[0] = static_cast<char>(value);
    p[1] = static_cast<char>(value >> 8);
    p[2] = static_cast<char>(value >> 16);
}

static std::runtime_error fse_error(const char *prefix, size_t code) {
    return std::runtime_error(std::string(prefix) + ": " + FSE_getErrorName(code));
}

void fse_encode(std::string_view in, std::string& out) {
    if (in.empty()) {
        out.assign(1, special_header(raw_block));
        return;
    }

    out.resize(FSE_compressBound(in.size()));
    size_t csize = FSE_compress(out.data(), out.size(), in.data(), in.size());
    if (FSE_isError(csize))
        throw fse_error("fse compress error", csize);

    if (csize > 1) {
        out.resize(csize);
        return;
    }

    if (csize == 1) {
        if (in.size() > erans_maxsize)
            throw std::runtime_error("fse rle block exceeds 16MiB");
        out.resize(5);
        out[0] = special_header(rle_block);
        write24(out.data() + 1, static_cast<u32>(in.size() - 1));
        out[4] = in[0];
        return;
    }

    out.resize(in.size() + 1);
    out[0] = special_header(raw_block);
    std::memcpy(out.data() + 1, in.data(), in.size());
}

void fse_decode(std::string_view in, std::string& out) {
    if (in.empty())
        throw std::runtime_error("empty fse block");

    if (is_special(in[0])) {
        auto block_type = static_cast<unsigned char>(in[0]) >> 4;
        if (block_type == raw_block) {
            if (in.size() - 1 > erans_maxsize)
                throw std::runtime_error("decoded fse raw block exceeds 16MiB");
            out.assign(in.substr(1));
            return;
        }
        if (block_type == rle_block) {
            if (in.size() != 5)
                throw std::runtime_error("bad fse rle block");
            u32 raw_size = read24(in.data() + 1) + 1;
            if (raw_size > erans_maxsize)
                throw std::runtime_error("decoded fse rle block exceeds 16MiB");
            out.assign(raw_size, in[4]);
            return;
        }
        throw std::runtime_error("unknown fse block type");
    }

    out.resize(erans_maxsize);
    size_t raw_size = FSE_decompress(out.data(), out.size(), in.data(), in.size());
    if (FSE_isError(raw_size))
        throw fse_error("fse decompress error", raw_size);
    out.resize(raw_size);
}
