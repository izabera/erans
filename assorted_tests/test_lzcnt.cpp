#include <iostream>
#include <immintrin.h>

int ctz_branch(int mask_gt) {
    return (mask_gt == 0) ? 15 : __builtin_ctz(mask_gt) - 1;
}

int lzcnt_branchless(int mask_gt) {
    unsigned int mask_le = (~mask_gt) & 0xFFFF;
    return 31 - _lzcnt_u32(mask_le);
}

int main() {
    bool passed = true;
    for (int i = 0; i <= 16; i++) {
        // mask_gt has 1s in the top (16-i) bits
        int mask_gt = 0;
        for (int j = i; j < 16; j++) {
            mask_gt |= (1 << j);
        }
        
        int r1 = ctz_branch(mask_gt);
        int r2 = lzcnt_branchless(mask_gt);
        
        if (r1 != r2) {
            std::cout << "Mismatch for i=" << i << " (mask=0x" << std::hex << mask_gt << std::dec << "): ctz=" << r1 << " lzcnt=" << r2 << std::endl;
            passed = false;
        }
    }
    if (passed) std::cout << "All valid mask_gt patterns match perfectly!\n";
    return 0;
}
