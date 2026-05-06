#include <iostream>
#include "types.hpp"
#include "shrub.hpp"

int main() {
    Shrub s;
    s.inc(0x00);
    s.inc(0x01);
    s.inc(0x10);
    s.inc(0x50);
    
    for (int i = 0; i < 16; i++) {
        std::cout << "top[" << i << "] = " << s.top[i] << "\n";
    }
    
    auto [c0, f0] = s.sym2cdf(0x00);
    std::cout << "sym2cdf(0x00): c=" << c0 << " f=" << f0 << "\n";
    auto [c1, f1] = s.sym2cdf(0x10);
    std::cout << "sym2cdf(0x10): c=" << c1 << " f=" << f1 << "\n";
    auto [c5, f5] = s.sym2cdf(0x50);
    std::cout << "sym2cdf(0x50): c=" << c5 << " f=" << f5 << "\n";

    return 0;
}
