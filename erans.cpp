#include "erans.hpp"
#include "shrub.hpp"
#include "types.hpp"
#include <cstring>

// in-chunk layout:
//   [ rANS-encoded bytes ] [ shrub histogram ] [ u32 hist_size (LE) ]
//
// the rANS bytes can be written directly to `out` as we encode; the histogram
// is only known after one pass, so it is appended at the end.  the trailer
// gives the decoder the histogram size so it can find the boundary (the
// histogram has variable length: 1 + 32*k binary + variable unary bytes).

void erans_encode(std::string_view in, std::string& out) {
    Shrub shrub;
    u64 state = 1;
    auto N = in.size();

    out.clear();
    out.reserve(N + 577 + 4 + 16);

    for (u64 M = 1; M <= N; M++) {
        u8 s = u8(in[N - M]);
        shrub.inc(s);
        auto [c, f] = shrub.sym2cdf(s);

        // post-encode invariant is state in [M, 256*M); the C step lands
        // there iff the pre-encode state is in [f, 256*f).  shift bytes
        // out while state >= 256*f.  the lower bound is automatic since
        // state >= M-1 >= f (the f == M case stays at state = 1 forever)
        while (state >= (u64(f) << 8)) {
            out.push_back(u8(state));
            state >>= 8;
        }

        state = (state / f) * M + (state % f) + c;
    }

    // flush state byte by byte; the decoder pulls them back from the tail
    while (state) {
        out.push_back(u8(state));
        state >>= 8;
    }

    // append the histogram (worst case 577 bytes), then trim to actual size
    auto rans_end = out.size();
    out.resize(rans_end + 577);
    auto hist_base = reinterpret_cast<u8*>(out.data()) + rans_end;
    auto hist_end  = shrub.encode(hist_base);
    u32 hist_size  = u32(hist_end - hist_base);

    out.resize(rans_end + hist_size + 4);
    std::memcpy(out.data() + rans_end + hist_size, &hist_size, 4);
}

void erans_decode(std::string_view in, std::string& out) {
    Shrub shrub;

    auto base   = reinterpret_cast<u8*>(const_cast<char*>(in.data()));
    auto in_end = base + in.size();

    u32 hist_size;
    std::memcpy(&hist_size, in_end - 4, 4);

    auto hist_start = in_end - 4 - hist_size;
    shrub.decode(hist_start);

    u32 total = 0;
    for (auto c : shrub.counts) total += c;

    out.clear();
    out.resize(total);

    // rANS bytes are [base, hist_start); pop from the tail (LIFO)
    auto tail = hist_start;

    u64 state = 0;
    for (u64 M = total; M >= 1; M--) {
        // pull bytes until state is back in [M, 256*M).
        // first iteration also reconstructs the encoder's flushed state.
        while (state < M && tail > base)
            state = (state << 8) | *--tail;

        u32 slot = state % M;
        Shrub::cf cf;
        u8 s = shrub.cdf2sym(slot, cf);
        shrub.dec(s);

        state = (state / M) * cf.f + (slot - cf.c);
        out[total - M] = char(s);
    }
}
