#pragma once
#include <immintrin.h>
#include <cstdint>

using i8  = int8_t ; using u8  = uint8_t ;
using i16 = int16_t; using u16 = uint16_t; using f16 = _Float16; using bf16 = __bf16;
using i32 = int32_t; using u32 = uint32_t; using f32 = _Float32;
using i64 = int64_t; using u64 = uint64_t; using f64 = _Float64;

template <typename t, int n>
struct simd {
    using unaligned __attribute__((vector_size(sizeof(t)*n),aligned(1)))           = t;
    using   aligned __attribute__((vector_size(sizeof(t)*n),aligned(sizeof(t)*n))) = t;

    constexpr static auto set1(t v) {
        if constexpr (n ==  4) return unaligned{v,v,v,v};
        if constexpr (n ==  8) return unaligned{v,v,v,v, v,v,v,v};
        if constexpr (n == 16) return unaligned{v,v,v,v, v,v,v,v, v,v,v,v, v,v,v,v};
        if constexpr (n == 32)
            return unaligned{
                v,v,v,v, v,v,v,v, v,v,v,v, v,v,v,v,
                v,v,v,v, v,v,v,v, v,v,v,v, v,v,v,v,
            };
        if constexpr (n == 64)
            return unaligned{
                v,v,v,v, v,v,v,v, v,v,v,v, v,v,v,v,
                v,v,v,v, v,v,v,v, v,v,v,v, v,v,v,v,
                v,v,v,v, v,v,v,v, v,v,v,v, v,v,v,v,
                v,v,v,v, v,v,v,v, v,v,v,v, v,v,v,v,
            };
    }
};

#define XAll(...) \
    X( i8,__VA_ARGS__); X( u8,__VA_ARGS__); \
    X(i16,__VA_ARGS__); X(u16,__VA_ARGS__); X(f16,__VA_ARGS__); X(bf16,__VA_ARGS__); \
    X(i32,__VA_ARGS__); X(u32,__VA_ARGS__); X(f32,__VA_ARGS__); \
    X(i64,__VA_ARGS__); X(u64,__VA_ARGS__); X(f64,__VA_ARGS__);

#define X(t,w) using t##x##w = simd<t,w>::aligned; using t##x##w##_u = simd<t,w>::unaligned;
#define Simd(w) XAll(w)

Simd(4)
Simd(8)
Simd(16)
Simd(32)
Simd(64)

#undef X
#undef XAll
#undef Simd
