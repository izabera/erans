#include "utils.hpp"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

[[noreturn]] void error(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
};

void *makecache(size_t size, const char *cache) {
    size_t page = 2 * 1024 * 1024;
    size = (size + page - 1) & ~(page-1);

    int flags = MAP_PRIVATE|MAP_ANONYMOUS;
    int fd = open(cache, O_RDWR|O_CREAT, 0644);
    if (fd != -1 && posix_fallocate(fd, 0, size) != -1)
        flags = MAP_SHARED;

    auto map = mmap(0, size, PROT_READ|PROT_WRITE, flags, fd, 0);
    if (fd != -1)
        close(fd);

    if (map == MAP_FAILED)
        error("could not create map");
    madvise(map, size, MADV_HUGEPAGE);
    return map;
}

Lemire::Lemire(u32 max, const char *cache) {
    auto size = (max+1) * sizeof *data;
    data = static_cast<u64*>(makecache(size, cache));
    madvise(data, size, MADV_SEQUENTIAL);

    // last value acts as a sentinel, as these values are never 0
    if (!data[max]) {
        // XXX: split in sections of 1mb so multiple threads can fill in
        for (u32 i = 2; i <= max; i++)
            data[i] = -1ull / i + 1;
    }
}

Log::Log(u32 max, const char *cache) {
    auto size = (max+1) * sizeof *data;
    data = static_cast<long double *>(makecache(size, cache));
    madvise(data, size, MADV_SEQUENTIAL);

    // last value acts as a sentinel, as these values are never 0
    if (!data[max]) {
        // XXX: split in sections of 1mb so multiple threads can fill in
        for (auto i = 0u; i <= max; i++)
            data[i] = std::logl(i);
    }
}
