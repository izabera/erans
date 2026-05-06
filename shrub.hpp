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

        u32 remainder = target - top[group];
        auto v_bottom = simd<u32,16>::set1(remainder);
        u32 mask_l = cmp_le_mask(bottom[group], v_bottom);
        u32 lane = 31 - __builtin_clz(mask_l);

        u8 s = (group << 4) | lane;
        cf.c = top[group] + reinterpret_cast<const u32_a*>(bottom)[s];
        cf.f = counts[s];
        return s;
    }

    // yolo
    u8* encode(u8 *bytes);
    u8* decode(u8 *bytes);

    void debug() const;
};
