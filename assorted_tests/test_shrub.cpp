#include <iostream>
#include "types.hpp"
#include "shrub.hpp"

int main() {
    Shrub s;
    s.inc(0x53);
    s.inc(0x54);
    s.inc(0x53);
    s.inc(0x12);
    auto [c1, f1] = s.sym2cdf(0x53);
    auto [c2, f2] = s.sym2cdf(0x54);
    auto [c3, f3] = s.sym2cdf(0x55);
    std::cout << "0x53: c=" << c1 << " f=" << f1 << "\n";
    std::cout << "0x54: c=" << c2 << " f=" << f2 << "\n";
    std::cout << "0x55: c=" << c3 << " f=" << f3 << "\n";
    return 0;
}
