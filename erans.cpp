#include "erans.hpp"
#include "lemire.hpp"
#include "shrub.hpp"
#include "types.hpp"
#include <cstring>
#include <span>

// in-chunk layout:
//   [ rANS-encoded bytes ] [ unary ] [ binary (32*k) ] [ k (1 byte) ]
//
// the decoder reads k from the last byte, finds the binary section
// (fixed size 32*k), then walks the unary section backward to learn
// where it starts -- that's also where rANS ends.

static lemire l;
void erans_encode_simple(std::string_view in, std::string& out) {
    Shrub shrub;

    u64 state = 1;
    auto N = in.size();

    // worst-case histogram size for k capped at 16
    // 1 (k) + 512 (binary, 32*k) + ceil((256 + N/65536) / 8) (unary)
    constexpr u32 max_hist = 1 + 512 + (256 + (erans_maxsize >> 16) + 7) / 8;

    // pre-resize so we can just write to the raw pointer
    out.resize(N + max_hist + 16 + 8);
    auto base = reinterpret_cast<u8*>(out.data());
    auto p = base;

    for (u64 M = 1; M <= N; M++) {
        u8 s = u8(in[M - 1]);
        auto [c, f] = shrub.sym2cdf_inc(s);

        // post-encode invariant is state in [M, 256*M); the C step lands
        // there iff the pre-encode state is in [f, 256*f).  shift bytes
        // out so that state < 256*f.  the lower bound is automatic since
        // state >= M-1 >= f (the f == M case stays at state = 1 forever)

        // branchless:
        // - 4 unrolled cmovs cover the worst case (f=1, state up to ~2^32)
        // - store 8 bytes of original state at p, advance p by the number of shifts taken

        // the tail bytes get overwritten by subsequent iterations or truncated at the end

        u64 limit = u64(f) << 8;
        u64 orig = state;

        bool b0 = state >= limit; state = b0 ? state >> 8 : state;
        bool b1 = state >= limit; state = b1 ? state >> 8 : state;
        bool b2 = state >= limit; state = b2 ? state >> 8 : state;
        bool b3 = state >= limit; state = b3 ? state >> 8 : state;

        u32 n = u32(b0) + u32(b1) + u32(b2) + u32(b3);
        std::memcpy(p, &orig, 8);
        p += n;

        u32 q = state, r = 0;
        if (f > 1) {
            [[likely]]; // almost always
            auto [d, m] = l.divmod(state, f);
            q = d;
            r = m;
        }
        state = q * M + r + c;
    }

    // flush state byte by byte; the decoder pulls them back from the tail
    while (state) {
        *p++ = u8(state);
        state >>= 8;
    }

    // append the histogram, growing backward into worst-case-reserved space
    // the actual histogram may be smaller, so memmove down to compact
    auto rans_end = size_t(p - base);
    out.resize(rans_end + max_hist);
    auto end_p   = reinterpret_cast<u8*>(out.data()) + rans_end + max_hist;
    auto start_p = shrub.encode_rev(end_p);
    auto hist_size = u32(end_p - start_p);
    auto rans_end_ptr = reinterpret_cast<u8*>(out.data()) + rans_end;
    if (start_p != rans_end_ptr)
        std::memmove(rans_end_ptr, start_p, hist_size);
    out.resize(rans_end + hist_size);
}

void erans_state::decode_shrub(std::string_view in) {
    shrub = {};

    auto base   = reinterpret_cast<const u8*>(in.data());
    auto in_end = const_cast<u8*>(base) + in.size();

    auto rans_end = shrub.decode_rev(in_end);
    stream = {base, size_t(rans_end - base)};
}

void erans_state::decode_to(std::span<u8> out) {
    // if (shrub.size() != out.size()) {
    //     fprintf(stderr, "BUG!!!! shrub.size()==%u out.size()==%zu\n", shrub.size(), out.size());
    //     exit(1);
    // }
    auto base = stream.data();
    auto tail = base + stream.size();

    u64 state = 0, M = out.size();

    // loop down to M == 2
    for (; M > 1; M--) {
        while (state < M && tail > base)
            state = (state << 8) | *--tail;

        auto [q, slot] = l.divmod(state, M);
        // if (q != state/M) {
        //     fprintf(stderr, "BUG!!!! %u/%u=%u total=%u\n", u32(state), u32(M), q, total);
        //     exit(1);
        // }
        // if (slot != state%M) {
        //     fprintf(stderr, "BUG!!!! %u%%%u=%u total=%u\n", u32(state), u32(M), slot, total);
        //     exit(1);
        // }
        Shrub::rem_f cf;
        u8 s = shrub.cdf2sym_dec(slot, cf);

        state = q * cf.f + cf.rem;
        out[M - 1] = s;
    }

    // state' = state/M * f + slot - c
    //
    // the final iteration has a bunch of nice properties
    // - M = 1
    // - state' = 1
    // - slot = 0
    // - c = 0
    // - f = 1
    //
    // 1 = state/1 * 1 + 0 - 0     =>    state = 1
    //
    // i.e. a stream with 1 symbol is a stream where all symbols are identical
    // and each symbol adds 0 information
    //
    // so we already know the final state, and we don't need to try to refill
    // the symbol is whatever is left in the shrub

    u32 s = shrub.lastsymbol();
    out[0] = s;
}

void erans_decode_simple(std::string_view in, std::string& out) {
    erans_state state;
    state.decode_shrub(in);
    out.resize(state.shrub.size());
    state.decode_to(std::span{reinterpret_cast<u8*>(out.data()), out.size()});
}
