#include "types.hpp"
#include <immintrin.h>
#include <iostream>

inline u32 cmp_gt_mask(u32x16 a, u32x16 b) {
#if defined(__AVX512F__)
    return _mm512_cmp_epi32_mask((__m512i)a, (__m512i)b, _MM_CMPINT_NLE);
#elif defined(__AVX2__)
    __m256i a_lo = *(__m256i*)&a;
    __m256i a_hi = *((__m256i*)&a + 1);
    __m256i b_lo = *(__m256i*)&b;
    __m256i b_hi = *((__m256i*)&b + 1);
    u32 m_lo = _mm256_movemask_ps((__m256)_mm256_cmpgt_epi32(a_lo, b_lo));
    u32 m_hi = _mm256_movemask_ps((__m256)_mm256_cmpgt_epi32(a_hi, b_hi));
    return m_lo | (m_hi << 8);
#else
    u32 mask = 0;
    auto cmp = a > b;
    for (int i = 0; i < 16; i++) {
        if (cmp[i]) mask |= (1 << i);
    }
    return mask;
#endif
}

int main() {
    u32x16 a = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    u32x16 b = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
    u32 m = cmp_gt_mask(a, b);
    std::cout << "mask: 0x" << std::hex << m << std::endl;
    return 0;
}
