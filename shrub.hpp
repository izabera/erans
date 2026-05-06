#include "types.hpp"
#include <cassert>

// a shrub is a 2 level tree that tracks a cdf

struct Shrub {
    u32x16 top{};        // cdf of all groups
    u32x16 bottom[16]{}; // cdf within each group

    u32 counts[256]{}; // tracking all counts makes a couple of things more efficient

    struct view { u32x16_u val; } __attribute__((packed,may_alias));


    static constexpr u32 scale[] = {
        0x0,0x1,0x2,0x3, 0x4,0x5,0x6,0x7,
        0x8,0x9,0xa,0xb, 0xc,0xd,0xe,0xf,
    };

    // this ends up being a little more efficient than the masked add
    static constexpr u32 bits[] = {
        0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0,
        1,1,1,1, 1,1,1,1,
        1,1,1,1, 1,1,1,1,
    };

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

    __attribute__((always_inline))
    cf sym2cdf(u8 byte) const {
        // every other way to write this generates horrible code
        using u32_a __attribute__((may_alias)) = u32;

        auto c = top[byte>>4] + reinterpret_cast<const u32_a*>(bottom)[byte];
        auto f = counts[byte];
        return {c, f};
    }

    struct cfs { u32 c, f, s; };

#if 0
    u8 cdf2sym(u32 value) {
        __m512i v = _mm512_set1_epi32(value);

        u16 le = _mm512_cmple_epi32_mask(top, v);
        u32 group = 31 - _lzcnt_u32(le);

        u32 remainder = value - top[group];
        u16 le2 = _mm512_cmple_epi32_mask(bottom[group], _mm512_set1_epi32(remainder));
        u32 lane = 31 - _lzcnt_u32(le2);

        return (group << 4) | lane;
    }
#endif

    __attribute__((always_inline))
    u8 cdf2sym(u32 v, cf& out_cf) const {
        auto v_target = u32x16{v,v,v,v, v,v,v,v, v,v,v,v, v,v,v,v};
        u32 mask_g = cmp_gt_mask(top, v_target);
        u32 group = (mask_g == 0) ? 15 : __builtin_ctz(mask_g) - 1;

        u32 r = v - top[group];
        auto v_ltarget = u32x16{r,r,r,r, r,r,r,r, r,r,r,r, r,r,r,r};
        u32 mask_l = cmp_gt_mask(bottom[group], v_ltarget);
        u32 lane = (mask_l == 0) ? 15 : __builtin_ctz(mask_l) - 1;

        u8 s = (group << 4) | lane;
        out_cf.c = top[group] + bottom[group][lane];
        out_cf.f = counts[s];
        return s;
    }

    void encode(u8 *out) const;
    void decode(const u8 *in);

    void debug() const;
};
