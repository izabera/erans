#pragma once
#include "types.hpp"

struct lemire {
    u64 *recs;
    lemire(u32 max = 1<<24, const char *cache = "/tmp/lemirecache");

    // this only works from 2 onwards

    __attribute__((always_inline))
    u32 div(u32 a, u32 b) const { return (u128(a) * recs[b]) >> 64; }

    __attribute__((always_inline))
    u32 mod(u32 a, u32 b) const {
        u64 lo = u128(a) * recs[b];
        return (lo * u128(b)) >> 64;
    }

    struct dm { u32 d, m; };

    __attribute__((always_inline))
    dm divmod(u32 a, u32 b) const {
        u32 q = div(a,b);
        return {q, a-q*b};
    }
};
