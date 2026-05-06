#include "types.hpp"
#include <immintrin.h>

#define MCABEGIN(name) asm volatile("# LLVM-MCA-BEGIN " #name);
#define MCAEND() asm volatile("# LLVM-MCA-END");

struct cf { u32 c, f; };

struct Shrub {
    u32x16 top{};
    u32x16 bottom[16]{};
    u32 counts[256]{};

    __attribute__((always_inline))
    u8 cdf2sym_1(u32 value) const {
        MCABEGIN(shrub_cdf2sym_1);
        auto vec = simd<u32,16>::set1(value);
        auto less1 = top <= vec;
        u32 sum1 = 0;
        auto ones1 = -less1;
        for (int i = 0; i < 16; ++i) sum1 += ones1[i];
        u32 group = sum1 - 1;

        u32 remainder = value - top[group];
        auto vec2 = simd<u32,16>::set1(remainder);
        auto less2 = bottom[group] <= vec2;
        u32 sum2 = 0;
        auto ones2 = -less2;
        for (int i = 0; i < 16; ++i) sum2 += ones2[i];
        u32 lane = sum2 - 1;

        u8 s = (group << 4) | lane;
        MCAEND();
        return s;
    }

    __attribute__((always_inline))
    u8 cdf2sym_2(u32 target) const {
        MCABEGIN(shrub_cdf2sym_2);
        __m256i v_target = _mm256_set1_epi32(target);

        __m256i v_top_lo = _mm256_load_si256(((__m256i*)&top));
        __m256i v_top_hi = _mm256_load_si256(((__m256i*)&top)+1);

        __m256i cmp_top_lo = _mm256_cmpgt_epi32(v_top_lo, v_target);
        __m256i cmp_top_hi = _mm256_cmpgt_epi32(v_top_hi, v_target);

        u32 mask_g = _mm256_movemask_ps(_mm256_castsi256_ps(cmp_top_lo)) | 
                    (_mm256_movemask_ps(_mm256_castsi256_ps(cmp_top_hi)) << 8);

        u32 g = (mask_g == 0) ? 15 : __builtin_ctz(mask_g) - 1;

        u32 local_target = target - top[g];
        __m256i v_ltarget = _mm256_set1_epi32(local_target);

        __m256i v_bot_lo = _mm256_load_si256(((__m256i*)&bottom[g]));
        __m256i v_bot_hi = _mm256_load_si256(((__m256i*)&bottom[g])+1);

        __m256i cmp_bot_lo = _mm256_cmpgt_epi32(v_bot_lo, v_ltarget);
        __m256i cmp_bot_hi = _mm256_cmpgt_epi32(v_bot_hi, v_ltarget);

        u32 mask_l = _mm256_movemask_ps(_mm256_castsi256_ps(cmp_bot_lo)) | 
                     (_mm256_movemask_ps(_mm256_castsi256_ps(cmp_bot_hi)) << 8);

        u32 l = (mask_l == 0) ? 15 : __builtin_ctz(mask_l) - 1;

        u8 s = (g << 4) | l;
        MCAEND();
        return s;
    }
};

u8 test1(const Shrub& s, u32 v) { return s.cdf2sym_1(v); }
u8 test2(const Shrub& s, u32 v) { return s.cdf2sym_2(v); }
