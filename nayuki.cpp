#include "nayuki.hpp"
#include "erans.hpp"

#include "ArithmeticCoder.hpp"
#include "BitIoStream.hpp"
#include "FrequencyTable.hpp"

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <vector>

using std::uint32_t;
using std::uint64_t;

static constexpr uint32_t eof_symbol = 256;
static constexpr uint32_t symbol_limit = 257;
static constexpr int state_bits = 32;

static uint32_t symbol_of(char ch) {
    return static_cast<unsigned char>(ch);
}

static BitInputStream bit_input(std::string_view in, std::istringstream& source) {
    source.str(std::string(in));
    source.clear();
    return BitInputStream(source);
}

void nayuki_static_encode(std::string_view in, std::string& out) {
    std::vector<uint32_t> counts(symbol_limit, 0);
    counts[eof_symbol] = 1;
    for (char ch : in)
        counts[symbol_of(ch)]++;

    SimpleFrequencyTable freqs(counts);
    std::ostringstream sink(std::ios::out | std::ios::binary);
    BitOutputStream bits(sink);

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t freq = freqs.get(i);
        for (int j = 31; j >= 0; j--)
            bits.write(static_cast<int>((freq >> j) & 1));
    }

    ArithmeticEncoder enc(state_bits, bits);
    for (char ch : in)
        enc.write(freqs, symbol_of(ch));
    enc.write(freqs, eof_symbol);
    enc.finish();
    bits.finish();

    out = sink.str();
}

void nayuki_static_decode(std::string_view in, std::string& out) {
    std::istringstream source(std::ios::in | std::ios::binary);
    BitInputStream bits = bit_input(in, source);

    std::vector<uint32_t> counts(symbol_limit, 0);
    uint64_t raw_size = 0;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t freq = 0;
        for (int j = 0; j < 32; j++)
            freq = (freq << 1) | static_cast<uint32_t>(bits.readNoEof());
        counts[i] = freq;
        raw_size += freq;
        if (raw_size > erans_maxsize)
            throw std::runtime_error("decoded static arithmetic block exceeds 16MiB");
    }
    counts[eof_symbol] = 1;

    SimpleFrequencyTable freqs(counts);
    ArithmeticDecoder dec(state_bits, bits);

    out.clear();
    out.reserve(static_cast<size_t>(raw_size));
    while (true) {
        uint32_t symbol = dec.read(freqs);
        if (symbol == eof_symbol)
            break;
        if (out.size() == raw_size)
            throw std::runtime_error("static arithmetic block overran its frequency table");
        out.push_back(static_cast<char>(symbol));
    }

    if (out.size() != raw_size)
        throw std::runtime_error("static arithmetic block ended before its frequency table");
}

void nayuki_adaptive_encode(std::string_view in, std::string& out) {
    SimpleFrequencyTable freqs{FlatFrequencyTable(symbol_limit)};
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

    SimpleFrequencyTable freqs{FlatFrequencyTable(symbol_limit)};
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
