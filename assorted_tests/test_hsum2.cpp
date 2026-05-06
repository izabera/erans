#include "types.hpp"

using V = u32x16;

u32 hsum(V ones) {
    auto v8 = ones + __builtin_shuffle(ones, ones, V{8,9,10,11,12,13,14,15, 0,1,2,3,4,5,6,7});
    auto v4 = v8 + __builtin_shuffle(v8, v8, V{4,5,6,7,0,1,2,3, 12,13,14,15,8,9,10,11});
    auto v2 = v4 + __builtin_shuffle(v4, v4, V{2,3,0,1, 6,7,4,5, 10,11,8,9, 14,15,12,13});
    auto v1 = v2 + __builtin_shuffle(v2, v2, V{1,0,3,2, 5,4,7,6, 9,8,11,10, 13,12,15,14});
    return v1[0];
}
