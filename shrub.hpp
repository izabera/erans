#pragma once
#include "types.hpp"
#include <cassert>

// a shrub is a 2 level tree that tracks a cdf

struct Shrub {
    i32x16 top{};        // cdf of all groups
    i32x16 bottom[16]{}; // cdf within each group

    u32 counts[256]{}; // tracking all counts makes a couple of things more efficient

    // this ends up being a little more efficient than the masked add
    static constexpr u32 bits[] = {
        0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0,
        1,1,1,1, 1,1,1,1,
        1,1,1,1, 1,1,1,1,
    };

    struct view { i32x16_u val; } __attribute__((packed,may_alias));

    // apparently gcc and clang can't go from bottom[g][l] to ((i32_a*)bottom)[byte]
    // every other way to write this generates horrible code
    using i32_a __attribute__((may_alias)) = i32;

    __attribute__((always_inline))
    void inc(u8 byte) {
        counts[byte]++;

        u32 hi = byte >> 4;
        u32 lo = byte & 15;

        top        += reinterpret_cast<const view*>(bits + 15 - hi)->val;
        bottom[hi] += reinterpret_cast<const view*>(bits + 15 - lo)->val;
    }

    __attribute__((always_inline))
    void dec(u8 byte) {
        counts[byte]--;

        u32 hi = byte >> 4;
        u32 lo = byte & 15;

        top        -= reinterpret_cast<const view*>(bits + 15 - hi)->val;
        bottom[hi] -= reinterpret_cast<const view*>(bits + 15 - lo)->val;
    }

    struct cf { u32 c, f; };

    __attribute__((always_inline))
    cf sym2cdf(u8 s) const {
        auto c = top[s>>4] + reinterpret_cast<const i32_a*>(bottom)[s];
        auto f = counts[s];
        return {u32(c), f};
    }

    // in the decoder we're always doing cdf2sym -> dec
    // (see older versions of this code)
    // there are a few ways to improve upon this:
    //
    // a<=b gives a vector of 0/-1 with -1 in lanes 0..group
    // dec needs to subtract 1 from lanes (group+1)..15, so we add ~cmp
    //
    // a>b gives -1 in lanes (group+1)..15, exactly what dec needs to add
    // tzcnt_u16 returns 16 on zero input, so target-in-group-15 falls out for free
    //
    // but clang avoids emitting tzctnw (it's a partial register stall)
    // instead it emits tzcnt(foo|0x10000)-1
    // either way this is slower than the alternative

    // also this returns slot - c instead of c to save a load
    // we'd recompute it in the outer loop anyway
    struct rem_f { u32 rem, f; };
    __attribute__((always_inline))
    u8 cdf2sym_dec(i32 target, rem_f& cf) {
        auto cmp_top = top > target;
        u32 group = 31 - __builtin_clz(to_mask(cmp_top) ^ 0xffffu);

        u32 top_c = reinterpret_cast<const i32_a*>(&top)[group];
        i32 remainder = target - top_c;
        top += cmp_top;

        auto cmp_bottom = bottom[group] > remainder;
        u32 lane = 31 - __builtin_clz(to_mask(cmp_bottom) ^ 0xffffu);
        u8 s = (group << 4) | lane;
        u32 bottom_c = reinterpret_cast<const i32_a*>(&bottom)[s];
        cf.rem = remainder - bottom_c;
        cf.f = counts[s]--;

        bottom[group] += cmp_bottom;
        return s;
    }

    // likewise, in the encoder we're always doing inc -> sym2cdf
    // fuse into one call to avoid redundant loads of counts[s] and top[hi]
    __attribute__((always_inline))
    cf sym2cdf_inc(u8 s) {
        u32 hi = s >> 4;
        u32 lo = s & 15;

        u32 c = top[hi] + reinterpret_cast<const i32_a*>(bottom)[s];
        u32 f = ++counts[s];

        // there's no immediate data dependency on top or bottom[hi],
        // so the latency of the loads doesn't matter
        top        += reinterpret_cast<const view*>(bits + 15 - hi)->val;
        bottom[hi] += reinterpret_cast<const view*>(bits + 15 - lo)->val;

        return {c, f};
    }

    // an extremely performance critical case that definitely needed to be special cased
    u8 lastsymbol() const {
        u32 hi = to_mask(top <= 0);
        u32 lz1 = 31 - __builtin_clz(hi);
        u32 lo = to_mask(bottom[lz1] <= 0);
        u32 lz2 = 31 - __builtin_clz(lo);
        return lz1 << 4 | lz2;
    }

    // yolo
    u8* encode(u8 *bytes);
    u8* decode(u8 *bytes);

    u8* encode_rev(u8 *end); // reverse layout: [..unary..][..binary..][k]
    u8* decode_rev(u8 *end); // both iterate from the end and return the start

    u32 size() const {
        u32 total = 0;
        for (auto c : counts)
            total += c;
        return total;
    }

    void rebuild();
    void debug() const;
};
