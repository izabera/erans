#include "erans.hpp"
#include "types.hpp"
#include <cstdio>
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

    std::string rbuf(erans_maxsize*2, '\0'), wbuf;
    wbuf.reserve(erans_maxsize*2);

    // really basic format:
    // <--hdr--> <-----frame----> <-----frame----> <-----frame----> 
    // [ magic ] [ [len] [data] ] [ [len] [data] ] [ [len] [data] ]

    constexpr static u32 magic = 0xf0cacc1a;

    u32 count, tmp;
    if (mode == "encode") {
        if (fwrite(&magic, sizeof magic, 1, out) != 1)
            error("io error");
        while ((count = fread(rbuf.data(), 1, erans_maxsize, in))) {
            erans_encode({rbuf.data(), count}, wbuf);
            count = wbuf.size();
            if (fwrite(&count, sizeof count, 1, out) != 1 ||
                fwrite(wbuf.data(), count, 1, out) != 1)
                error("io error");
        }
    }
    else if (mode == "decode") {
        if (fread(&tmp, sizeof tmp, 1, in) != 1 || tmp != magic)
            error("bad magic");
        while (fread(&count, sizeof count, 1, in)) {
            if (fread(rbuf.data(), count, 1, in) != 1)
                error("io error");
            erans_decode({rbuf.data(), count}, wbuf);
            if (fwrite(wbuf.data(), wbuf.size(), 1, out) != 1)
                error("write error");
        }
    }
}
