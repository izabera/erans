#pragma once
#include "types.hpp"

[[noreturn]] void error(const char *msg);

void *makecache(size_t size, const char *cache);

struct Lemire {
    u64 *data;
    Lemire(u32 max = 1<<24, const char *cache = "/tmp/lemirecache");

    // this only works from 2 onwards

    __attribute__((always_inline))
    u32 div(u32 a, u32 b) const { return (u128(a) * data[b]) >> 64; }

    __attribute__((always_inline))
    u32 mod(u32 a, u32 b) const {
        u64 lo = u128(a) * data[b];
        return (lo * u128(b)) >> 64;
    }

    struct dm { u32 d, m; };

    __attribute__((always_inline))
    dm divmod(u32 a, u32 b) const {
        u32 q = div(a,b);
        return {q, a-q*b};
    }
};

#include <math.h>

struct Log {
    long double *data = nullptr;
    Log(u32 max = 1<<24, const char *cache = "/tmp/logcache");
    auto operator()(std::integral auto n) const { return data[n]; }
    auto operator()(auto n) const { return std::logl(n); }
};
