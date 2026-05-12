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

    __attribute__((always_inline))
    u8 cdf2sym(u32 target, cf& cf) const {
        auto v_top = simd<u32,16>::set1(target);
        u32 mask_g = cmp_le_mask(top, v_top);
        u32 group = 31 - __builtin_clz(mask_g);

        // auto top_c = top[group];
        // it doesn't make sense to me but this is faster
        auto top_c = reinterpret_cast<const i32_a*>(&top)[group];
        u32 remainder = target - top_c;
        auto v_bottom = simd<u32,16>::set1(remainder);
        u32 mask_l = cmp_le_mask(bottom[group], v_bottom);
        u32 lane = 31 - __builtin_clz(mask_l);

        u8 s = (group << 4) | lane;
        cf.c = top_c + reinterpret_cast<const i32_a*>(bottom)[s];
        cf.f = counts[s];
        return s;
    }

    // in the decoder we're always doing cdf2sym -> dec
    // there are a few ways to improve upon this:
    //
    // a<=b gives a vector of 0/-1 with -1 in lanes 0..group
    // dec needs to subtract 1 from lanes (group+1)..15, so we add ~cmp
    //
    // a>b gives -1 in lanes (group+1)..15, exactly what dec needs to add
    // tzcnt_u16 returns 16 on zero input, so target-in-group-15 falls out for free

    __attribute__((always_inline))
    u8 cdf2sym_dec(u32 target, cf& cf) {
        auto v_top = simd<i32,16>::set1(target);
#ifdef USE_TZCNT
        auto cmp_top = top > v_top;
        u32 group = _tzcnt_u16(to_mask(cmp_top)) - 1;

        u32 top_c = reinterpret_cast<const i32_a*>(&top)[group];
        u32 remainder = target - top_c;
        top += cmp_top;

        auto v_bottom = simd<u32,16>::set1(remainder);
        auto cmp_bottom = bottom[group] > v_bottom;
        u32 lane = _tzcnt_u16(to_mask(cmp_bottom)) - 1;
#else
        auto cmp_top = top <= v_top;
        u32 group = 31 - __builtin_clz(to_mask(cmp_top));

        u32 top_c = reinterpret_cast<const i32_a*>(&top)[group];
        u32 remainder = target - top_c;
        top += ~cmp_top;

        auto v_bottom = simd<i32,16>::set1(remainder);
        auto cmp_bottom = bottom[group] <= v_bottom;
        u32 lane = 31 - __builtin_clz(to_mask(cmp_bottom));
#endif
        u8 s = (group << 4) | lane;
        cf.c = top_c + reinterpret_cast<const i32_a*>(bottom)[s];
        cf.f = counts[s]--;

#ifdef USE_TZCNT
        bottom[group] += cmp_bottom;
#else
        bottom[group] += ~cmp_bottom;
#endif
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
        u32 hi = to_mask(top > 0);
        u32 tz1 = __tzcnt_u16(hi) - 1;
        u32 lo = to_mask(bottom[tz1] > 0);
        u32 tz2 = __tzcnt_u16(lo) - 1;
        return tz1 << 4 | tz2;
    }

    // yolo
    u8* encode(u8 *bytes);
    u8* decode(u8 *bytes);

    u8* encode_rev(u8 *end); // reverse layout: [..unary..][..binary..][k]
    u8* decode_rev(u8 *end); // both iterate from the end and return the start

    void debug() const;
};
