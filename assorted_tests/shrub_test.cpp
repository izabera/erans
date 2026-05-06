#include "shrub.hpp"
#include <iostream>
#include <string>
#include <cassert>

int main() {
    Shrub s;
    std::string test_data = "this is a test string with some characters!@#$%^&*()_+~`1234567890-=\\][{}|;':\",./<>?";

    // increment characters
    for (auto c : test_data)
        s.inc(static_cast<u8>(c));

    // test that cdf2sym correctly reverses sym2cdf for every character
    int total_counts = 0;
    bool all_passed = true;

    for (int i = 0; i < 256; i++) {
        if (s.counts[i] > 0) {
            auto [c, f] = s.sym2cdf(i);

            // any target in the range [c, c + f - 1] should resolve back to i
            for (u32 target = c; target < c + f; target++) {
                Shrub::cf out_cf;
                u8 sym = s.cdf2sym(target, out_cf);
                if (sym != i || out_cf.c != c || out_cf.f != f) {
                    std::cout << "ERROR for target " << target 
                              << " (expected sym=" << i << " c=" << c << " f=" << f << ")\n";
                    std::cout << "got: sym=" << (int)sym << " c=" << out_cf.c << " f=" << out_cf.f << "\n";
                    all_passed = false;
                }
            }
            total_counts += f;
        }
    }

    if (!all_passed)
        return 1;
    std::cout << "ok: all " << total_counts << " targets in the test string correctly mapped by cdf2sym\n";

    // additional dynamic edge case tests
    Shrub s2;
    s2.inc(0x00);
    s2.inc(0x10);
    s2.inc(0x10);
    s2.inc(0xFF);
    s2.inc(0xFF);
    s2.inc(0xFF);

    auto verify = [&](u32 target, u8 expected_sym, u32 expected_c, u32 expected_f) {
        Shrub::cf cf;
        u8 sym = s2.cdf2sym(target, cf);
        if (sym != expected_sym || cf.c != expected_c || cf.f != expected_f) {
            std::cout << "dynamic edge case ERROR for target " << target << "\n";
            std::cout << "expected: sym=0x" << std::hex << (int)expected_sym 
                      << " c=" << std::dec << expected_c << " f=" << expected_f << "\n";
            std::cout << "got: sym=0x" << std::hex << (int)sym 
                      << " c=" << std::dec << cf.c << " f=" << cf.f << "\n";
            exit(1);
        }
    };

    // 0x00 -> c=0, f=1 (target 0)
    // 0x10 -> c=1, f=2 (targets 1, 2)
    // 0xFF -> c=3, f=3 (targets 3, 4, 5)
    verify(0, 0x00, 0, 1);
    verify(1, 0x10, 1, 2);
    verify(2, 0x10, 1, 2);
    verify(3, 0xFF, 3, 3);
    verify(4, 0xFF, 3, 3);
    verify(5, 0xFF, 3, 3);

    std::cout << "ok: dynamic edge cases\n";

    return 0;
}
