#include "shrub.hpp"
#include <iostream>
#include <cstdio>

void Shrub::debug() const {
    auto show = [](auto &vec, auto prefix) {
        std::cout << prefix << ' ';
        for (auto i = 0; i < 16; i++)
            std::cout << vec[i] << ' ';
        std::cout << std::endl;
    };
    show(top, "top:       ");
    for (auto i = 0; i < 16; i++) {
        char prefix[20];
        sprintf(prefix, "bottom[%2d]:", i);
        show(bottom[i], prefix);
    }
};

//  8b   24b      256 * x       256 * y
// [k] [total] [rice-binary] [rice-unary]
//
// k is the rice parameter
// it maxes out at 16, and higher values can be used as sentinels
//
// total is actually stored as total-1
u32 Shrub::encode(u8 *out) const {
    u32 len = 0;

    u32 total = 0;
    for (auto c : counts)
        total += c;
    total -= 1;

    return len;
};

void Shrub::decode(const u8 *out, u32 size) {
    u32 total = 0;
};
