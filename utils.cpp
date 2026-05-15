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
        struct stat st{};
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
        if (!data[max]) { // these values too are never 0 (unless max == 1 i guess)
            for (auto i = 0u; i <= max; i++)
                data[i] = std::logl(i);
        }
    };

    data = makecache<long double>(size, cache, fill);
    madvise(data, size, MADV_SEQUENTIAL);
}
#endif

#if 0
#include "erans.hpp"
#include <cstring>
#include <string>
struct fileio::impl {
    constexpr static u32 magic = 0xf0cacc1a;

    size_t size = 0;
    size_t off = 0;
    FILE *fptr = nullptr;
    u8 *map = nullptr;

    std::string buf;
    bool is_input, is_encoder;

    impl(const char *name, bool is_input, bool is_encoder, size_t size_hint) :
        is_input(is_input), is_encoder(is_encoder)
    {
        int fd;
        if (is_input)
            fd = open(name, O_RDONLY);
        else
            fd = open(name, O_RDWR|O_CREAT|O_TRUNC, 0644);
        if (fd == -1)
            error("could not open file");

        struct stat st{};
        fstat(fd, &st);

        size = is_input ? st.st_size : size_hint;
        auto mapsize = round_up(size);

        if (size > 0) {
            if (is_input)
                map = static_cast<u8*>(mmap(0, PROT_READ, mapsize, MAP_PRIVATE, fd, 0));
            else {
                ftruncate(fd, size);
                map = static_cast<u8*>(mmap(0, PROT_READ|PROT_WRITE, mapsize, MAP_SHARED, fd, 0));
            }
        }

        if (map != nullptr && map != MAP_FAILED) {
            close(fd);
            madvise(map, size, MADV_SEQUENTIAL);
            madvise(map, size, MADV_HUGEPAGE);
        }
        else
            fptr = fdopen(fd, is_input ? "rb" : "wb");

        if (is_input) {
            u32 tmp{};
            auto buf = get(4);
            std::memcpy(&tmp, buf.data(), buf.size());
            if (tmp != magic)
                error("bad magic");
        }
        else {
        }
    }

    ~impl() {
        if (fptr)
            fclose(fptr);
        else if (map)
            munmap(map, size);
    }

    bool advance() { return {}; }
    std::span<u8> get(size_t size = erans_maxsize) { return {}; }
};

fileio::fileio(const char *name, bool is_input, bool is_encoder, size_t size_hint) :
    ioimpl(new impl(name, is_input, is_encoder, size_hint)) {}
fileio::~fileio() { delete ioimpl; }

std::span<u8> fileio::get() const { return ioimpl->get(); }
bool fileio::advance() { return ioimpl->advance(); }
#endif
