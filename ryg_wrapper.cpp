#include "ryg_wrapper.hpp"
#include "erans.hpp"
#include "shrub.hpp"
#include "types.hpp"

#include "rans64.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static constexpr u32 prob_bits = 16;
static constexpr u32 prob_scale = 1u << prob_bits;

// Worst-case size of Shrub::encode_rev() for a 16MiB block.
static constexpr u32 max_hist_size =
    1 + 512 + (256 + (erans_maxsize >> 16) + 7) / 8;

struct RygStats {
    std::array<u32, 256> freqs{};
    std::array<u32, 257> cum_freqs{};

    RygStats(const u32 counts[256], bool already_normalized = false) {
        for (size_t i = 0; i < freqs.size(); i++)
            freqs[i] = counts[i];
        if (already_normalized)
            calc_cum_freqs();
        else
            normalize();
    }

    void calc_cum_freqs() {
        cum_freqs[0] = 0;
        for (size_t i = 0; i < freqs.size(); i++)
            cum_freqs[i + 1] = cum_freqs[i] + freqs[i];
    }

    void normalize() {
        calc_cum_freqs();
        u32 total = cum_freqs[256];
        if (total == 0)
            throw std::runtime_error("cannot encode empty ryg rans block");

        for (size_t i = 1; i <= 256; i++)
            cum_freqs[i] = (u64(prob_scale) * cum_freqs[i]) / total;

        for (size_t i = 0; i < 256; i++) {
            if (freqs[i] == 0 || cum_freqs[i + 1] != cum_freqs[i])
                continue;

            u32 best_freq = ~u32{};
            size_t best_steal = 256;
            for (size_t j = 0; j < 256; j++) {
                u32 freq = cum_freqs[j + 1] - cum_freqs[j];
                if (freq > 1 && freq < best_freq) {
                    best_freq = freq;
                    best_steal = j;
                }
            }
            if (best_steal == 256)
                throw std::runtime_error("failed to normalize ryg rans frequencies");

            if (best_steal < i) {
                for (size_t j = best_steal + 1; j <= i; j++)
                    cum_freqs[j]--;
            } else {
                for (size_t j = i + 1; j <= best_steal; j++)
                    cum_freqs[j]++;
            }
        }

        for (size_t i = 0; i < 256; i++)
            freqs[i] = cum_freqs[i + 1] - cum_freqs[i];
    }
};

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
    throw std::runtime_error("bad varint in ryg rans block");
}

void ryg_rans64_encode(std::string_view in, std::string& out) {
    if (in.size() > erans_maxsize)
        throw std::runtime_error("ryg rans input block exceeds 16MiB");

    Shrub counts;
    for (char ch : in)
        counts.counts[static_cast<unsigned char>(ch)]++;
    RygStats stats(counts.counts);

    Rans64EncSymbol syms[256];
    for (size_t i = 0; i < 256; i++)
        Rans64EncSymbolInit(&syms[i], stats.cum_freqs[i], stats.freqs[i], prob_bits);

    size_t max_stream_words = in.size() / sizeof(u32) + 1024;
    std::vector<u32> stream(max_stream_words);
    u32 *stream_end = stream.data() + stream.size();
    u32 *ptr = stream_end;

    Rans64State rans;
    Rans64EncInit(&rans);
    for (size_t i = in.size(); i > 0; i--) {
        auto symbol = static_cast<unsigned char>(in[i - 1]);
        Rans64EncPutSymbol(&rans, &ptr, &syms[symbol], prob_bits);
    }
    Rans64EncFlush(&rans, &ptr);

    size_t stream_size = size_t(stream_end - ptr) * sizeof(u32);
    out.clear();
    append_varu32(out, static_cast<u32>(in.size()));
    out.append(reinterpret_cast<const char*>(ptr), stream_size);

    size_t rans_end = out.size();
    out.resize(rans_end + max_hist_size);
    Shrub model;
    for (size_t i = 0; i < stats.freqs.size(); i++)
        model.counts[i] = stats.freqs[i];

    auto base = reinterpret_cast<u8*>(out.data());
    auto hist_end = base + out.size();
    auto hist_start = model.encode_rev(hist_end);
    auto hist_size = size_t(hist_end - hist_start);
    auto rans_end_ptr = base + rans_end;
    if (hist_start != rans_end_ptr)
        std::memmove(rans_end_ptr, hist_start, hist_size);
    out.resize(rans_end + hist_size);
}

void ryg_rans64_decode(std::string_view in, std::string& out) {
    auto base = reinterpret_cast<const u8*>(in.data());
    auto in_end = const_cast<u8*>(base) + in.size();

    std::string_view stream_prefix = in;
    u32 raw_size = read_varu32(stream_prefix);
    if (raw_size > erans_maxsize)
        throw std::runtime_error("decoded ryg rans block exceeds 16MiB");
    size_t prefix_size = in.size() - stream_prefix.size();

    Shrub model;
    auto stream_end = model.decode_rev(in_end);
    if (model.size() != prob_scale)
        throw std::runtime_error("bad ryg rans model total");

    auto stream_start = base + prefix_size;
    if (stream_end < stream_start)
        throw std::runtime_error("bad ryg rans stream bounds");

    size_t stream_size = stream_end - stream_start;
    if (stream_size % sizeof(u32) != 0 || stream_size < 2 * sizeof(u32))
        throw std::runtime_error("bad ryg rans stream size");

    RygStats stats(model.counts, true);

    std::vector<u8> cum2sym(prob_scale);
    for (size_t s = 0; s < 256; s++)
        std::fill(cum2sym.begin() + stats.cum_freqs[s],
                  cum2sym.begin() + stats.cum_freqs[s + 1],
                  static_cast<u8>(s));

    Rans64DecSymbol syms[256];
    for (size_t i = 0; i < 256; i++)
        Rans64DecSymbolInit(&syms[i], stats.cum_freqs[i], stats.freqs[i]);

    std::vector<u32> stream(stream_size / sizeof(u32));
    std::memcpy(stream.data(), stream_start, stream_size);
    u32 *ptr = stream.data();

    Rans64State rans;
    Rans64DecInit(&rans, &ptr);

    out.resize(raw_size);
    for (size_t i = 0; i < raw_size; i++) {
        u32 slot = Rans64DecGet(&rans, prob_bits);
        u32 symbol = cum2sym[slot];
        out[i] = static_cast<char>(symbol);
        Rans64DecAdvanceSymbol(&rans, &ptr, &syms[symbol], prob_bits);
    }
}
