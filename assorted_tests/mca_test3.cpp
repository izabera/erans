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
    u8 cdf2sym(u32 target) const {
        MCABEGIN(shrub_cdf2sym_avx512);
        
        __m512i v_target = _mm512_set1_epi32(target);
        __m512i v_top = _mm512_load_si512((const __m512i*)&top);
        u16 mask_g = _mm512_cmpgt_epi32_mask(v_top, v_target);
        u32 g = (mask_g == 0) ? 15 : __builtin_ctz(mask_g) - 1;

        u32 local_target = target - top[g];
        __m512i v_ltarget = _mm512_set1_epi32(local_target);
        __m512i v_bot = _mm512_load_si512((const __m512i*)&bottom[g]);
        u16 mask_l = _mm512_cmpgt_epi32_mask(v_bot, v_ltarget);
        u32 l = (mask_l == 0) ? 15 : __builtin_ctz(mask_l) - 1;

        u8 s = (g << 4) | l;
        MCAEND();
        return s;
    }
};

u8 test(const Shrub& s, u32 v) { return s.cdf2sym(v); }
