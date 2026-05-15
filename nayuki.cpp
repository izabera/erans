#include "nayuki.hpp"
#include "erans.hpp"

#include "ArithmeticCoder.hpp"
#include "BitIoStream.hpp"
#include "FrequencyTable.hpp"

#include <cstring>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <vector>

using std::uint32_t;
using std::uint64_t;

static constexpr uint32_t eof_symbol = 256;
static constexpr uint32_t byte_symbol_limit = 256;
static constexpr uint32_t adaptive_symbol_limit = 257;
static constexpr int state_bits = 32;

// Worst-case size of Shrub::encode_rev() for a 16MiB block:
// 1 byte k + 32*k bytes binary section + unary-coded high parts.
static constexpr uint32_t max_hist_size =
    1 + 512 + (256 + (erans_maxsize >> 16) + 7) / 8;

static uint32_t symbol_of(char ch) {
    return static_cast<unsigned char>(ch);
}

static BitInputStream bit_input(std::string_view in, std::istringstream& source) {
    source.str(std::string(in));
    source.clear();
    return BitInputStream(source);
}

void nayuki_static_encode(std::string_view in, std::string& out) {
    Shrub shrub;
    for (char ch : in)
        shrub.counts[symbol_of(ch)]++;

    std::vector<uint32_t> counts(shrub.counts, shrub.counts + byte_symbol_limit);
    SimpleFrequencyTable freqs(counts);
    std::ostringstream sink(std::ios::out | std::ios::binary);
    BitOutputStream bits(sink);

    ArithmeticEncoder enc(state_bits, bits);
    for (char ch : in)
        enc.write(freqs, symbol_of(ch));
    enc.finish();
    bits.finish();

    out = sink.str();
    auto stream_size = out.size();
    out.resize(stream_size + max_hist_size);

    auto base = reinterpret_cast<u8*>(out.data());
    auto hist_end = base + out.size();
    auto hist_start = shrub.encode_rev(hist_end);
    auto hist_size = size_t(hist_end - hist_start);
    auto stream_end = base + stream_size;
    if (hist_start != stream_end)
        std::memmove(stream_end, hist_start, hist_size);
    out.resize(stream_size + hist_size);
}

void nayuki_static_decode(std::string_view in, std::string& out) {
    auto base = reinterpret_cast<const u8*>(in.data());
    auto in_end = const_cast<u8*>(base) + in.size();

    Shrub shrub;
    auto stream_end = shrub.decode_rev(in_end);
    uint32_t raw_size = shrub.size();
    if (raw_size > erans_maxsize)
        throw std::runtime_error("decoded static arithmetic block exceeds 16MiB");

    std::string_view stream(reinterpret_cast<const char*>(base), stream_end - base);
    std::istringstream source(std::ios::in | std::ios::binary);
    BitInputStream bits = bit_input(stream, source);
    std::vector<uint32_t> counts(shrub.counts, shrub.counts + byte_symbol_limit);
    SimpleFrequencyTable freqs(counts);
    ArithmeticDecoder dec(state_bits, bits);

    out.resize(raw_size);
    for (uint32_t i = 0; i < raw_size; i++) {
        uint32_t symbol = dec.read(freqs);
        out[i] = static_cast<char>(symbol);
    }
}

void nayuki_adaptive_encode(std::string_view in, std::string& out) {
    SimpleFrequencyTable freqs{FlatFrequencyTable(adaptive_symbol_limit)};
    std::ostringstream sink(std::ios::out | std::ios::binary);
    BitOutputStream bits(sink);

    ArithmeticEncoder enc(state_bits, bits);
    for (char ch : in) {
        uint32_t symbol = symbol_of(ch);
        enc.write(freqs, symbol);
        freqs.increment(symbol);
    }
    enc.write(freqs, eof_symbol);
    enc.finish();
    bits.finish();

    out = sink.str();
}

void nayuki_adaptive_decode(std::string_view in, std::string& out) {
    std::istringstream source(std::ios::in | std::ios::binary);
    BitInputStream bits = bit_input(in, source);

    SimpleFrequencyTable freqs{FlatFrequencyTable(adaptive_symbol_limit)};
    ArithmeticDecoder dec(state_bits, bits);

    out.clear();
    while (true) {
        uint32_t symbol = dec.read(freqs);
        if (symbol == eof_symbol)
            break;
        if (out.size() == erans_maxsize)
            throw std::runtime_error("decoded adaptive arithmetic block exceeds 16MiB");
        out.push_back(static_cast<char>(symbol));
        freqs.increment(symbol);
    }
}
