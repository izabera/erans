#include "types.hpp"

using V = u32x16;

u32 hsum(V ones) {
    u32 sum = 0;
    for (int i = 0; i < 16; ++i) sum += ones[i];
    return sum;
}
