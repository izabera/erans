#include <iostream>
#include "types.hpp"

struct Shrub {
    u32x16 top{};
    u32x16 bottom[16]{};
    u32 counts[256]{};

    struct view { u32x16_u val; } __attribute__((packed,may_alias));

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
        using u32_a __attribute__((may_alias)) = u32;
        auto c = top[byte>>4] + reinterpret_cast<const u32_a*>(bottom)[byte];
        auto f = counts[byte];
        return {c, f};
    }

    __attribute__((always_inline))
    u8 cdf2sym(u32 value, cf& cf) {
        auto &[c, f] = cf;
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
        c = top[group] + bottom[group][lane];
        f = counts[s];
        return s;
    }
};

int main() {
    Shrub s;
    s.inc(0x53);
    s.inc(0x54);
    s.inc(0x53);
    s.inc(0x12);
    
    // total count = 4.
    // 0x12 -> count 1, cdf 0
    // 0x53 -> count 2, cdf 1
    // 0x54 -> count 1, cdf 3
    
    Shrub::cf cf;
    u8 sym = s.cdf2sym(0, cf);
    std::cout << "target 0: sym=0x" << std::hex << (int)sym << " c=" << cf.c << " f=" << cf.f << "\n";
    sym = s.cdf2sym(1, cf);
    std::cout << "target 1: sym=0x" << std::hex << (int)sym << " c=" << cf.c << " f=" << cf.f << "\n";
    sym = s.cdf2sym(2, cf);
    std::cout << "target 2: sym=0x" << std::hex << (int)sym << " c=" << cf.c << " f=" << cf.f << "\n";
    sym = s.cdf2sym(3, cf);
    std::cout << "target 3: sym=0x" << std::hex << (int)sym << " c=" << cf.c << " f=" << cf.f << "\n";

    return 0;
}