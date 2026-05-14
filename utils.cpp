#include "utils.hpp"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

[[noreturn]] void error(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
};

// round everything up to 2mb to allow using thp where possible
constexpr static size_t round_up(size_t size) {
    constexpr static size_t page = 2 * 1024 * 1024;
    return (size + page - 1) & ~(page - 1);
}

template <typename t>
t *makecache(size_t size, const char *cache, auto fill) {
    size = round_up(size);

    int flags = MAP_PRIVATE|MAP_ANONYMOUS;
    int fd = open(cache, O_RDWR|O_CREAT, 0644);

    if (fd != -1) {
        flock(fd, LOCK_EX);
        struct stat st;
        fstat(fd, &st);

        if (size_t(st.st_size) >= size || ftruncate(fd, size) == 0)
            flags = MAP_SHARED;
    }

    auto map = mmap(0, size, PROT_READ|PROT_WRITE, flags, fd, 0);

    if (map == MAP_FAILED)
        error("could not create map");

    madvise(map, size, MADV_HUGEPAGE);
    #ifdef MADV_COLLAPSE
    madvise(map, size, MADV_COLLAPSE);
    #endif

    auto ret = static_cast<t*>(map);
    fill(ret);

    if (fd != -1) {
        flock(fd, LOCK_UN);
        close(fd);
    }

    return ret;
}

Lemire::Lemire(u32 max, const char *cache) {
    auto size = round_up((max+1) * sizeof *data);

    auto fill = [&](auto data) {
        // last value acts as a sentinel, as these values are never 0
        if (!data[max]) {
            // XXX: split in sections of 1mb so multiple threads can fill in
            for (u32 i = 2; i <= max; i++)
                data[i] = -1ull / i + 1;
        }
    };
    data = makecache<u64>(size, cache, fill);
    madvise(data, size, MADV_SEQUENTIAL);
}

#ifdef PRINT_STATS
Log::Log(u32 max, const char *cache) {
    auto size = round_up((max+1) * sizeof *data);

    auto fill = [&](auto data) {
        if (!data[max]) { // these values too are never 0
            for (auto i = 0u; i <= max; i++)
                data[i] = std::logl(i);
        }
    };

    data = makecache<long double>(size, cache, fill);
    madvise(data, size, MADV_SEQUENTIAL);
}
#endif
