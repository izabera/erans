#include "erans.hpp"
#include "lemire.hpp"
#include "shrub.hpp"
#include "types.hpp"
#include <cstring>
#include <memory>

// in-chunk layout:
//   [ rANS stream 0 ] ... [ rANS stream 3 ] [ stream sizes ] [ histograms ]
//
// Each rANS stream has its own final histogram and local M.  Histograms are
// stored at the tail, so the decoder can peel them off in reverse order.

static lemire l;
constexpr u32 rans_streams = erans_stream_count;
constexpr u32 rans_stream_mask = rans_streams - 1;
static_assert((rans_streams & rans_stream_mask) == 0);

void erans_encode(std::string_view in, std::string& out) {
    Shrub shrubs[rans_streams];

    u64 states[rans_streams];
    u32 totals[rans_streams]{};
    for (auto& state : states)
        state = 1;
    auto N = in.size();

    // worst-case per-stream histogram size:
    // 1 (k) + 544 (binary, 32*17) + ceil((256 + stream_max/65536) / 8) (unary)
    constexpr u32 max_hist = 1 + 544 + (256 + (erans_stream_maxsize >> 16) + 7) / 8;

    std::unique_ptr<u8[]> rans[rans_streams];
    u8* rans_pos[rans_streams];
    auto stream_cap = 4 * ((N + rans_streams - 1) / rans_streams) + 16;
    for (u32 i = 0; i < rans_streams; i++) {
        rans[i] = std::make_unique<u8[]>(stream_cap);
        rans_pos[i] = rans[i].get();
    }

    for (u64 M = 1; M <= N; M++) {
        u8 s = u8(in[M - 1]);
        u32 stream_idx = M & rans_stream_mask;
        auto [c, f] = shrubs[stream_idx].sym2cdf_inc(s);
        u32 stream_M = ++totals[stream_idx];
        u64& state = states[stream_idx];

        // Shift bytes out so that state < 256*f.  With interleaved states,
        // a stream can legitimately have state < M when its own byte tail is
        // empty, so the decoder refill is also per-stream.

        // branchless:
        // - 4 unrolled cmovs cover the worst case (f=1, state up to ~2^32)
        // - store 8 bytes of original state, advance by the number of shifts taken

        // the tail bytes get overwritten by subsequent iterations or truncated at the end

        u64 limit = u64(f) << 8;
        u64 orig = state;

        bool b0 = state >= limit; state = b0 ? state >> 8 : state;
        bool b1 = state >= limit; state = b1 ? state >> 8 : state;
        bool b2 = state >= limit; state = b2 ? state >> 8 : state;
        bool b3 = state >= limit; state = b3 ? state >> 8 : state;

        u32 n = u32(b0) + u32(b1) + u32(b2) + u32(b3);
        std::memcpy(rans_pos[stream_idx], &orig, 8);
        rans_pos[stream_idx] += n;

        u32 q = state, r = 0;
        if (f > 1) {
            [[likely]]; // almost always
            auto [d, m] = l.divmod(state, f);
            q = d;
            r = m;
        }
        state = q * stream_M + r + c;
    }

    // Flush each state to its own byte stream.  The decoder pulls bytes back
    // from the corresponding stream tail.
    auto flush = [&](u32 stream_idx) {
        u64 state = states[stream_idx];
        while (state) {
            *rans_pos[stream_idx]++ = u8(state);
            state >>= 8;
        }
    };
    for (u32 i = 0; i < rans_streams; i++)
        flush(i);

    // append the histogram, growing backward into worst-case-reserved space
    // the actual histogram may be smaller, so memmove down to compact
    u32 rans_size[rans_streams];
    size_t rans_end = rans_streams * sizeof(u32);
    for (u32 i = 0; i < rans_streams; i++) {
        rans_size[i] = u32(rans_pos[i] - rans[i].get());
        rans_end += rans_size[i];
    }

    out.resize(rans_end + rans_streams * max_hist);
    auto base = reinterpret_cast<u8*>(out.data());
    auto p = base;
    for (u32 i = 0; i < rans_streams; i++) {
        std::memcpy(p, rans[i].get(), rans_size[i]);
        p += rans_size[i];
    }
    for (u32 i = 0; i < rans_streams; i++) {
        u32 stream_size = rans_size[i];
        std::memcpy(p, &stream_size, sizeof stream_size);
        p += sizeof stream_size;
    }

    auto end_p = base + rans_end + rans_streams * max_hist;
    auto start_p = end_p;
    for (u32 i = rans_streams; i-- > 0;)
        start_p = shrubs[i].encode_rev(start_p);
    auto hist_size = u32(end_p - start_p);
    auto rans_end_ptr = base + rans_end;
    if (start_p != rans_end_ptr)
        std::memmove(rans_end_ptr, start_p, hist_size);
    out.resize(rans_end + hist_size);
}

void erans_decode(std::string_view in, std::string& out) {
    Shrub shrubs[rans_streams];

    auto base   = reinterpret_cast<u8*>(const_cast<char*>(in.data()));
    auto in_end = base + in.size();

    auto hist_start = in_end;
    for (u32 i = rans_streams; i-- > 0;)
        hist_start = shrubs[i].decode_rev(hist_start);
    auto sizes_p = hist_start - rans_streams * sizeof(u32);

    u8* stream_base[rans_streams];
    u8* tails[rans_streams];
    auto p = base;
    for (u32 i = 0; i < rans_streams; i++) {
        u32 stream_size;
        std::memcpy(&stream_size, sizes_p + i * sizeof stream_size, sizeof stream_size);
        stream_base[i] = p;
        p += stream_size;
        tails[i] = p;
    }

    u32 totals[rans_streams]{};
    u32 total = 0;
    for (u32 i = 0; i < rans_streams; i++) {
        for (auto c : shrubs[i].counts) totals[i] += c;
        total += totals[i];
    }

    out.clear();
    out.resize(total);

    u64 states[rans_streams]{};
    u64 M = total;

    for (; M > 0; M--) {
        u32 stream_idx = M & rans_stream_mask;
        u32 stream_M = totals[stream_idx];
        if (stream_M == 1) {
            out[M - 1] = char(shrubs[stream_idx].lastsymbol());
            totals[stream_idx] = 0;
            continue;
        }

        u64& state = states[stream_idx];
        auto& tail = tails[stream_idx];
        while (state < stream_M && tail > stream_base[stream_idx])
            state = (state << 8) | *--tail;

        auto [q, slot] = l.divmod(state, stream_M);
        // if (q != state/M) {
        //     fprintf(stderr, "BUG!!!! %u/%u=%u total=%u\n", u32(state), u32(M), q, total);
        //     exit(1);
        // }
        // if (slot != state%M) {
        //     fprintf(stderr, "BUG!!!! %u%%%u=%u total=%u\n", u32(state), u32(M), slot, total);
        //     exit(1);
        // }
        Shrub::cf cf;
        u8 s = shrubs[stream_idx].cdf2sym_dec(slot, cf);

        state = q * cf.f + (slot - cf.c);
        totals[stream_idx] = stream_M - 1;
        out[M - 1] = char(s);
    }

#if 0
    while (state < M && tail > base)
        state = (state << 8) | *--tail;

    u32 q = state, slot = 0;
    Shrub::cf cf;
    u8 s = shrub.cdf2sym_dec(slot, cf);

    state = q * cf.f + (slot - cf.c);
    // if (state != 1) {
    //     fprintf(stderr, "BUG!!!! decoder state = %u %u%%%u=%u total=%u\n", u32(state), u32(M), slot, total);
    //     exit(1);
    // }
    out[M - 1] = char(s);
#endif

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

    // Each stream handles its own final symbol in the main loop.
}
