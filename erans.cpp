#include "erans.hpp"
#include "utils.hpp"
#include "shrub.hpp"
#include "types.hpp"
#include <cstring>
#include <span>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

// in-chunk layout:
//   [ rANS-encoded bytes ] [ unary ] [ binary (32*k) ] [ k (1 byte) ]
//
// the decoder reads k from the last byte, finds the binary section
// (fixed size 32*k), then walks the unary section backward to learn
// where it starts -- that's also where rANS ends.

static Lemire l;

#ifdef PRINT_STATS
static Log Log;
#endif

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

#ifdef PRINT_STATS
    long double eh = 0; // enumerative entropy
#endif

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

#ifdef PRINT_STATS
        eh -= Log(f);
#endif
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

#ifdef PRINT_STATS
    eh += std::lgammal(N);

    long double h = 0; // classic shannon entropy
    for (auto f : shrub.counts) {
        if (f)
            h -= f * (Log(f) - Log(N));
    }

    auto eh_bytes = eh / Log(2) / 8;
    auto h_bytes = h / Log(2) / 8;

    fprintf(stderr, "\n\n");
    fprintf(stderr, "enumerative limit in bytes:        %15.5Lf\n", eh_bytes);
    fprintf(stderr, "shannon limit in bytes:            %15.5Lf\n", h_bytes);
    fprintf(stderr, "shannon overhead over enumerative: %15.5Lf\n", h_bytes-eh_bytes);
    fprintf(stderr, "optimal cost of histogram:         %15.5Lf\n", h_bytes-eh_bytes);
    fprintf(stderr, "erans stream length:               %15.5Lf\n", (long double)rans_end);
    fprintf(stderr, "erans hist length:                 %15.5Lf\n", (long double)hist_size);
    fprintf(stderr, "erans total length:                %15.5Lf\n", (long double)out.size());
    fprintf(stderr, "erans renorm overhead:             %15.5Lf\n", rans_end-eh_bytes);
    fprintf(stderr, "erans overhead per byte:           %15.5Lf\n", (out.size()-eh_bytes)/out.size());
    fprintf(stderr, "\n\n");
#endif
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

    auto pull_byte = [&] { state = (state << 8) | *--tail; };

    // refills up to n bytes
    auto refill = [&]<auto n> {
        if constexpr (n >= 4) { if (state >= M) return; pull_byte(); }
        if constexpr (n >= 3) { if (state >= M) return; pull_byte(); }
        if constexpr (n >= 2) { if (state >= M) return; pull_byte(); }
        if constexpr (n >= 1) { if (state >= M) return; pull_byte(); }
    };

    auto decode_one = [&] {
        auto [q, slot] = l.divmod(state, M);
        Shrub::rem_f cf;
        u8 s = shrub.cdf2sym_dec(slot, cf);

        state = q * cf.f + cf.rem;
        out[M - 1] = s;
    };

    // the first refill reads the encoder's final state flush
    // after that, each symbol can only have emitted this many renorm bytes
    while (state < M && tail > base)
        pull_byte();

    auto loop = [&]<auto n> {
        auto min_M = 1ull << ((n-1) * 8);
        for (; M > min_M && tail - base >= n; M--) {
            refill.template operator()<n>();
            decode_one();
        }
    };

    loop.operator()<4>();
    loop.operator()<3>();
    loop.operator()<2>();
    loop.operator()<1>();

    // loop<0> either decoded down to M == 1 or consumed the last rans byte
    // but the residual state may still identify a mixed prefix
    // e.g.: a short block run like "aabbcc" reaches here with no bytes left
    // and decodes the c/b symbols while state >= M
    for (; M > 1 && state >= M; M--)
        decode_one();

    // on the very final iteration:
    //
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

    // so, after breaking out of the previous loop, the state is 1
    // the remaining prefix is a run of the last symbol in the shrub
    // and we don't need the full decode and state change

    u32 s = shrub.lastsymbol();
    std::memset(out.data(), s, M);

    // if (state != 1) {
    //     fprintf(stderr, "BUG!!!! state==%u but it should be 1\n", u32(state));
    //     exit(1);
    // }
}

void erans_decode_simple(std::string_view in, std::string& out) {
    erans_state state;
    state.decode_shrub(in);
    out.resize(state.shrub.size());
    state.decode_to(std::span{reinterpret_cast<u8*>(out.data()), out.size()});
}
