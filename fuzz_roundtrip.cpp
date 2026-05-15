#include "erans.hpp"
#include "types.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#ifndef FUZZ_MAX_GENERATED
#define FUZZ_MAX_GENERATED (1u << 20)
#endif

static constexpr size_t max_generated =
    std::min<size_t>(size_t(FUZZ_MAX_GENERATED), size_t(erans_maxsize));

static uint64_t load64(const uint8_t *data, size_t size, size_t off) {
    uint64_t x = 0;
    for (size_t i = 0; i < 8 && off + i < size; i++)
        x |= uint64_t(data[off + i]) << (8 * i);
    return x;
}

static uint64_t mix64(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    x ^= x >> 31;
    return x;
}

static uint8_t next_byte(uint64_t &state) {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return uint8_t(state >> 56);
}

static size_t interesting_len(uint64_t x) {
    static constexpr size_t interesting[] = {
        0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64,
        127, 128, 255, 256, 257, 511, 512, 1023, 1024,
        4095, 4096, 65535, 65536, 65537,
    };

    if ((x & 3) == 0) {
        size_t n = interesting[(x >> 2) % std::size(interesting)];
        return std::min(n, max_generated);
    }
    return size_t(x % (max_generated + 1));
}

static void fail_mismatch(std::string_view input,
                          std::string_view encoded,
                          std::string_view decoded) {
    size_t first = 0;
    size_t common = std::min(input.size(), decoded.size());
    while (first < common && input[first] == decoded[first])
        first++;

    std::fprintf(stderr,
                 "erans roundtrip mismatch: raw=%zu encoded=%zu decoded=%zu first=%zu\n",
                 input.size(), encoded.size(), decoded.size(), first);
    std::abort();
}

static void check_roundtrip(std::string_view input) {
    std::string encoded;
    std::string decoded;

    erans_encode_simple(input, encoded);
    erans_decode_simple(encoded, decoded);

    if (decoded != input)
        fail_mismatch(input, encoded, decoded);
}

static std::string generated_case(const uint8_t *data, size_t size) {
    uint64_t seed = mix64(load64(data, size, 1) ^ (uint64_t(size) << 32));
    size_t len = interesting_len(load64(data, size, 9) ^ seed);
    std::string out(len, '\0');

    uint8_t mode = size ? data[0] % 8 : 0;
    uint8_t a = uint8_t(seed);
    uint8_t b = uint8_t(seed >> 8);
    uint8_t step = uint8_t((seed >> 16) | 1);

    switch (mode) {
    case 0:
        std::fill(out.begin(), out.end(), char(a));
        break;

    case 1: {
        size_t run = 1 + ((seed >> 24) & 255);
        for (size_t i = 0; i < out.size(); i++)
            out[i] = char(((i / run) & 1) ? b : a);
        break;
    }

    case 2: {
        unsigned alphabet = 1u + uint8_t(seed >> 32);
        for (char &ch : out)
            ch = char(next_byte(seed) % alphabet);
        break;
    }

    case 3: {
        size_t period = 2 + ((seed >> 40) & 4095);
        for (size_t i = 0; i < out.size(); i++)
            out[i] = char((i % period) == 0 ? b : a);
        break;
    }

    case 4:
        for (size_t i = 0; i < out.size(); i++)
            out[i] = char(uint8_t(a + i * step));
        break;

    case 5:
        for (size_t i = 0; i < out.size(); i++) {
            uint8_t raw = size ? data[(i % std::max<size_t>(size, 1))] : uint8_t(i);
            out[i] = char(raw ^ uint8_t(i >> 8));
        }
        break;

    case 6:
        for (size_t i = 0; i < out.size(); i++)
            out[i] = char(uint8_t(i));
        break;

    case 7:
        for (size_t i = 0; i < out.size(); i++)
            out[i] = char(next_byte(seed));
        break;
    }

    return out;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size <= erans_maxsize)
        check_roundtrip({reinterpret_cast<const char *>(data), size});

    auto expanded = generated_case(data, size);
    check_roundtrip(expanded);

    return 0;
}
