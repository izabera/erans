#include <iostream>
#include "types.hpp"
#include "shrub.hpp"

int main() {
    Shrub s;
    for (int i=0; i<256; i++) s.counts[i] = 0; // initialize!
    
    // Test my fixed logic
    auto test_inc = [&](u8 byte) {
        s.counts[byte]++;
        u32 hi = byte >> 4;
        u32 lo = byte & 15;
        s.top        += reinterpret_cast<const Shrub::view*>(Shrub::bits + 15 - hi)->val;
        s.bottom[hi] += reinterpret_cast<const Shrub::view*>(Shrub::bits + 15 - lo)->val;
    };

    test_inc(0x00);
    test_inc(0x01);
    test_inc(0x10);
    test_inc(0x50);
    
    for (int i = 0; i < 16; i++) {
        std::cout << "top[" << i << "] = " << s.top[i] << "\n";
    }
    
    auto print_sym2cdf = [&](u8 byte) {
        auto c = s.top[byte>>4] + reinterpret_cast<const u32*>(&s.bottom[0])[byte];
        auto f = s.counts[byte];
        std::cout << "sym2cdf(0x" << std::hex << (int)byte << "): c=" << std::dec << c << " f=" << f << "\n";
    };
    print_sym2cdf(0x00);
    print_sym2cdf(0x01);
    print_sym2cdf(0x10);
    print_sym2cdf(0x50);

    return 0;
}