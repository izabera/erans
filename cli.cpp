#include "erans.hpp"
#include "types.hpp"
#include <cstdio>
#include <chrono>
#include <string>

int main(int argc, char **argv) {
    auto error = [](const char *msg = "usage: cli encode|decode infile outfile") {
        fprintf(stderr, "%s\n", msg);
        exit(1);
    };

    if (argc != 4)
        error();

    auto mode = std::string(argv[1]);
    if (mode != "encode" && mode != "decode")
        error();

    auto in  = fopen(argv[2], "rb");
    auto out = fopen(argv[3], "wb");
    if (!in || !out)
        error("could not open file");

    std::string rbuf(erans_maxsize*2, '\0'), wbuf;
    wbuf.reserve(erans_maxsize*2);

    // really basic format:
    // <--hdr--> <-----frame----> <-----frame----> <-----frame----> 
    // [ magic ] [ [len] [data] ] [ [len] [data] ] [ [len] [data] ]

    constexpr static u32 magic = 0xf0cacc1a;

    u32 count, tmp;
    u64 total = 0;
    auto t0 = std::chrono::steady_clock::now();
    if (mode == "encode") {
        if (fwrite(&magic, sizeof magic, 1, out) != 1)
            error("io error");
        while ((tmp = fread(rbuf.data(), 1, erans_maxsize, in))) {
            erans_encode({rbuf.data(), tmp}, wbuf);
            count = wbuf.size();
            if (fwrite(&count, sizeof count, 1, out) != 1 ||
                fwrite(wbuf.data(), 1, count, out) != count)
                error("io error");
            auto t1 = std::chrono::steady_clock::now();
            total += tmp;
            fprintf(stderr, "%.2f MiB/s     \r", (total/1e6) / ((t1-t0).count()/1e9));
        }
    }
    else if (mode == "decode") {
        if (fread(&tmp, sizeof tmp, 1, in) != 1 || tmp != magic)
            error("bad magic");
        while (fread(&count, sizeof count, 1, in)) {
            if (fread(rbuf.data(), count, 1, in) != 1)
                error("io error");
            erans_decode({rbuf.data(), count}, wbuf);
            if (fwrite(wbuf.data(), 1, wbuf.size(), out) != wbuf.size())
                error("write error");
            auto t1 = std::chrono::steady_clock::now();
            total += count;
            fprintf(stderr, "%.2f MiB/s     \r", (total/1e6) / ((t1-t0).count()/1e9));
        }
    }
    fprintf(stderr, "\n");
}
