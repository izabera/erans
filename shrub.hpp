#include "types.hpp"
#include <cassert>

// a shrub is a 2 level tree that tracks a cdf

struct Shrub {
    u32x16 top{};        // cdf of all groups
    u32x16 bottom[16]{}; // cdf within each group

    u32 counts[256]{}; // tracking all counts makes a couple of things more efficient

    // this ends up being a little more efficient than the masked add
    static constexpr u32 bits[] = {
        0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0,
        1,1,1,1, 1,1,1,1,
        1,1,1,1, 1,1,1,1,
    };

    struct view { u32x16_u val; } __attribute__((packed,may_alias));

    // apparently gcc and clang can't go from bottom[g][l] to ((u32_a*)bottom)[byte]
    // every other way to write this generates horrible code
    using u32_a __attribute__((may_alias)) = u32;

    void inc(u8 byte) {
        counts[byte]++;

        u32 hi = byte >> 4;
        u32 lo = byte & 15;

        top        += reinterpret_cast<const view*>(bits + 15 - hi)->val;
        bottom[hi] += reinterpret_cast<const view*>(bits + 15 - lo)->val;
    }

    void dec(u8 byte) {
        counts[byte]--;

        u32 hi = byte >> 4;
        u32 lo = byte & 15;

        top        -= reinterpret_cast<const view*>(bits + 15 - hi)->val;
        bottom[hi] -= reinterpret_cast<const view*>(bits + 15 - lo)->val;
    }

    struct cf { u32 c, f; };

    cf sym2cdf(u8 s) const {
        auto c = top[s>>4] + reinterpret_cast<const u32_a*>(bottom)[s];
        auto f = counts[s];
        return {c, f};
    }

    u8 cdf2sym(u32 target, cf& cf) const {
        auto v_top = simd<u32,16>::set1(target);
        u32 mask_g = cmp_le_mask(top, v_top);
        u32 group = 31 - __builtin_clz(mask_g);

        // auto top_c = top[group];
        // it doesn't make sense to me but this is faster
        auto top_c = reinterpret_cast<const u32_a*>(&top)[group];
        u32 remainder = target - top_c;
        auto v_bottom = simd<u32,16>::set1(remainder);
        u32 mask_l = cmp_le_mask(bottom[group], v_bottom);
        u32 lane = 31 - __builtin_clz(mask_l);

        u8 s = (group << 4) | lane;
        cf.c = top_c + reinterpret_cast<const u32_a*>(bottom)[s];
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

    u8 cdf2sym_dec(u32 target, cf& cf) {
        auto v_top = simd<u32,16>::set1(target);
#ifdef USE_TZCNT
        auto cmp_top = top > v_top;
        u32 group = _tzcnt_u16(to_mask(cmp_top)) - 1;

        u32 top_c = reinterpret_cast<const u32_a*>(&top)[group];
        u32 remainder = target - top_c;
        top += cmp_top;

        auto v_bottom = simd<u32,16>::set1(remainder);
        auto cmp_bottom = bottom[group] > v_bottom;
        u32 lane = _tzcnt_u16(to_mask(cmp_bottom)) - 1;
#else
        auto cmp_top = top <= v_top;
        u32 group = 31 - __builtin_clz(to_mask(cmp_top));

        u32 top_c = reinterpret_cast<const u32_a*>(&top)[group];
        u32 remainder = target - top_c;
        top += ~cmp_top;

        auto v_bottom = simd<u32,16>::set1(remainder);
        auto cmp_bottom = bottom[group] <= v_bottom;
        u32 lane = 31 - __builtin_clz(to_mask(cmp_bottom));
#endif

        u8 s = (group << 4) | lane;
        cf.c = top_c + reinterpret_cast<const u32_a*>(bottom)[s];
        cf.f = counts[s]--;

#ifdef USE_TZCNT
        bottom[group] += cmp_bottom;
#else
        bottom[group] += ~cmp_bottom;
#endif
        return s;
    }

    // yolo
    u8* encode(u8 *bytes);
    u8* decode(u8 *bytes);

    // reverse layout: [..unary..][..binary..][k].  encode_rev grows backward
    // from `end` and returns the start of the histogram; decode_rev consumes
    // backward from `end` and returns the start it found.
    u8* encode_rev(u8 *end);
    u8* decode_rev(u8 *end);

    void debug() const;
};
