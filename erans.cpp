#include "erans.hpp"
#include "shrub.hpp"
#include "types.hpp"

// output layout:
//   [ shrub histogram (variable, <= 577 bytes) ] [ rANS-encoded bytes ]
//
// the decoder reads the shrub from the front to recover the counts,
// then pops bytes from the tail (rANS is a stack, LIFO).

void erans_encode(std::string_view in, std::string& out) {
    Shrub shrub;
    u64 state = 1;
    auto N = in.size();

    // emit rANS bytes into a scratch buffer; we'll prepend the histogram
    // once we've finished (only known after one full pass)
    std::string encoded;
    encoded.reserve(N + 16);

    for (u64 M = 1; M <= N; M++) {
        u8 s = u8(in[N - M]);
        shrub.inc(s);
        auto [c, f] = shrub.sym2cdf(s);

        // post-encode invariant is state in [M, 256*M); the C step lands
        // there iff the pre-encode state is in [f, 256*f).  shift bytes
        // out while state >= 256*f.  the lower bound is automatic since
        // state >= M-1 >= f (the f == M case stays at state = 1 forever)
        while (state >= (u64(f) << 8)) {
            encoded.push_back(u8(state));
            state >>= 8;
        }

        state = (state / f) * M + (state % f) + c;
    }

    // flush state byte by byte; the decoder pulls them back from the tail
    while (state) {
        encoded.push_back(u8(state));
        state >>= 8;
    }

    out.clear();
    out.resize(577); // shrub upper bound: 1 byte k + 32*k binary + ~64 unary
    auto base = reinterpret_cast<u8*>(out.data());
    auto end  = shrub.encode(base);
    out.resize(end - base);
    out.append(encoded);
}

void erans_decode(std::string_view in, std::string& out) {
    Shrub shrub;

    // Shrub::decode wants a non-const pointer but only reads
    auto base = reinterpret_cast<u8*>(const_cast<char*>(in.data()));
    auto in_end = base + in.size();
    auto p = shrub.decode(base);

    u32 total = 0;
    for (auto c : shrub.counts) total += c;

    out.clear();
    out.resize(total);

    // bytes [p, in_end) are the rANS stream; pop from the tail
    auto tail = in_end;

    u64 state = 0;
    for (u64 M = total; M >= 1; M--) {
        // pull bytes (LIFO) until state is back in [M, 256*M).
        // first iteration also reconstructs the encoder's flushed state.
        while (state < M && tail > p)
            state = (state << 8) | *--tail;

        u32 slot = state % M;
        Shrub::cf cf;
        u8 s = shrub.cdf2sym(slot, cf);
        shrub.dec(s);

        state = (state / M) * cf.f + (slot - cf.c);
        out[total - M] = char(s);
    }
}
