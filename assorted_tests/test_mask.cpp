#include "types.hpp"

using V = u32x16;

u32 test_mask(V top, u32 target) {
    V v_target = simd<u32, 16>::set1(target);
    auto cmp = top > v_target; // Returns a vector of -1 or 0
    // How to get a scalar mask from cmp?
    // One way: use __builtin_ia32_movmskps but that's intrinsic.
    // What if we just do some shifts and sums?
    // Or maybe we can cast it to a smaller vector and movemask?
    
    // Let's try to see if GCC provides a way.
    // What if we do:
    u32 mask = 0;
    for (int i = 0; i < 16; ++i) {
        if (cmp[i]) mask |= (1 << i);
    }
    return mask;
}
