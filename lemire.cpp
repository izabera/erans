#include "lemire.hpp"
#include "utils.hpp"
#include <fcntl.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
// #include <cstdio>

lemire::lemire(u32 max, const char *cache) {
    size_t len = max * sizeof(u64) + 4096;
    auto last = len / sizeof(u64);
    // fprintf(stderr, "lemire max=%u last=%zu    \n", max, last);

    int flags = MAP_PRIVATE|MAP_ANONYMOUS;
    int fd = open(cache, O_RDWR|O_CREAT, 0644);
    if (fd != -1 && posix_fallocate(fd, 0, len) != -1)
        flags = MAP_SHARED;

    auto map = mmap(0, len, PROT_READ|PROT_WRITE, flags, fd, 0);
    if (map == MAP_FAILED)
        error("could not load reciprocals");
    madvise(map, len, MADV_SEQUENTIAL);
    madvise(map, len, MADV_HUGEPAGE);
    if (fd != -1)
        close(fd);
    recs = static_cast<u64*>(map);

    // XXX: split in sections of 1mb so multiple threads can fill in

    auto fill = [](u64 *begin, u64 *end, u32 lo) {
        while (begin < end)
            *begin++ = -1ull / lo++ + 1;
    };

    if (!recs[last-1]) // acts as a sentinel, these values are never 0
        fill(recs+2, recs+last, 2);
}

#if 0
#include <cstdio>
int main() {
    lemire l;
    auto test = [&](u32 a, u32 b) {
        if (a / b != l.div(a,b)) {
            printf("%u / %u = %u != %u\n", a, b, a/b, l.div(a,b));
            return false;
        }
        return true;
    };
    test(1000, 10);
    test(12345, 678);
    test(99999999, 999);
}
#endif
