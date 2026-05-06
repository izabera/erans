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
    u8 cdf2sym_shuffle(u32 value) const {
        MCABEGIN(shrub_cdf2sym_shuffle);
        auto vec = simd<u32,16>::set1(value);
        auto less1 = top <= vec;
        auto ones1 = -less1;
        
        using V = u32x16;
        auto s1_8 = ones1 + __builtin_shuffle(ones1, ones1, V{8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7});
        auto s1_4 = s1_8 + __builtin_shuffle(s1_8, s1_8, V{4,5,6,7,0,1,2,3, 12,13,14,15,8,9,10,11});
        auto s1_2 = s1_4 + __builtin_shuffle(s1_4, s1_4, V{2,3,0,1, 6,7,4,5, 10,11,8,9, 14,15,12,13});
        auto s1_1 = s1_2 + __builtin_shuffle(s1_2, s1_2, V{1,0,3,2, 5,4,7,6, 9,8,11,10, 13,12,15,14});
        u32 group = s1_1[0] - 1;

        u32 remainder = value - top[group];
        auto vec2 = simd<u32,16>::set1(remainder);
        auto less2 = bottom[group] <= vec2;
        auto ones2 = -less2;

        auto s2_8 = ones2 + __builtin_shuffle(ones2, ones2, V{8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7});
        auto s2_4 = s2_8 + __builtin_shuffle(s2_8, s2_8, V{4,5,6,7,0,1,2,3, 12,13,14,15,8,9,10,11});
        auto s2_2 = s2_4 + __builtin_shuffle(s2_4, s2_4, V{2,3,0,1, 6,7,4,5, 10,11,8,9, 14,15,12,13});
        auto s2_1 = s2_2 + __builtin_shuffle(s2_2, s2_2, V{1,0,3,2, 5,4,7,6, 9,8,11,10, 13,12,15,14});
        u32 lane = s2_1[0] - 1;

        u8 s = (group << 4) | lane;
        MCAEND();
        return s;
    }
};

u8 test(const Shrub& s, u32 v) { return s.cdf2sym_shuffle(v); }
