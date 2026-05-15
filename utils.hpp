#pragma once
#include "types.hpp"
#include <span>

[[noreturn]] void error(const char *msg);

struct Lemire {
    u64 *data;
    Lemire(u32 max = 1<<24, const char *cache = "/tmp/lemirecache");

    // this only works from 2 onwards

    __attribute__((always_inline))
    u64 div(u64 a, u32 b) const { return (u128(a) * data[b]) >> 64; }

    __attribute__((always_inline))
    u32 mod(u64 a, u32 b) const {
        u64 lo = u128(a) * data[b];
        return (lo * u128(b)) >> 64;
    }

    struct dm { u64 d; u32 m; };

    __attribute__((always_inline))
    dm divmod(u64 a, u32 b) const {
        u32 q = div(a,b);
        return {q, u32(a-q*b)};
    }
};

#ifdef PRINT_STATS
#include <cmath>
#include <concepts>

struct Log {
    long double *data = nullptr;
    Log(u32 max = 1<<24, const char *cache = "/tmp/logcache");
    auto operator()(std::integral auto n) const { return data[n]; }
    auto operator()(auto n) const { return std::logl(n); }
};
#endif

struct fileio {
    struct impl;
    impl *ioimpl;

    fileio(const char *name, bool is_input, bool is_encoder, size_t size_hint);
    ~fileio();
    fileio(fileio&&) = delete;
    fileio(const fileio&) = delete;
    fileio& operator=(fileio&&) = delete;
    fileio& operator=(const fileio&) = delete;

    struct it {
        fileio *ptr;
        bool operator==(const it&) const = default;
        bool operator!=(const it&) const = default;
        auto operator++() { if (!ptr->advance()) ptr = nullptr; }
        auto operator*() const { return ptr->get(); }
    };
    it begin() { return {this}; }
    it end() const { return {}; }

    std::span<u8> get() const; // gets buffer
    bool advance(); // invalidates contents of buffer
};
