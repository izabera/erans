#include "erans.hpp"
#include "utils.hpp"
#include "types.hpp"
#include <cstdio>
#include <chrono>
#include <span>
#include <string>

int main(int argc, char **argv) {
    auto usage = "usage:\n"
                 "    erans-cli encode [infile [outfile]]\n"
                 "    erans-cli decode [infile [outfile]]\n"
               //"    erans-cli info   [file]\n"
                 ;

    if (argc < 2)
        error(usage);

    auto mode = std::string(argv[1]);
    if (mode != "encode" && mode != "decode")
        error(usage);

    auto in  = argc > 2 ? fopen(argv[2], "rb") : stdin;
    auto out = argc > 3 ? fopen(argv[3], "wb") : stdout;
    if (!in || !out)
        error("could not open file");

    std::string rbuf(erans_maxsize*2, '\0'), wbuf;
    wbuf.reserve(erans_maxsize*2);
    auto rptr = reinterpret_cast<u8*>(rbuf.data());
    auto wptr = reinterpret_cast<u8*>(wbuf.data());

    // really basic format:
    // <--hdr--> <-----frame----> <-----frame----> <-----frame----> 
    // [ magic ] [ [len] [data] ] [ [len] [data] ] [ [len] [data] ]

    constexpr static u32 magic = 0xf0cacc1a;

    u32 raw_count = 0, enc_count = 0, tmp;
    u64 raw_total = 0, enc_total = 0;

    auto t0 = std::chrono::steady_clock::now();
    auto progress = [&] {
        auto t1 = std::chrono::steady_clock::now();
        auto s = (t1-t0).count()/1e9;
        raw_total += raw_count;
        enc_total += enc_count;
        fprintf(stderr, "\r%s: raw=%7.2fMB - %6.2fMB/s   enc=%7.2fMB - %6.2f MB/s   ",
                mode.data(),
                raw_total/1e6, (raw_total/1e6) / s,
                enc_total/1e6, (enc_total/1e6) / s);
    };

    if (mode == "encode") {
        if (fwrite(&magic, sizeof magic, 1, out) != 1)
            error("io error");
        while ((raw_count = fread(rptr, 1, erans_maxsize, in))) {
            erans_encode_simple({rbuf.data(), raw_count}, wbuf);
            enc_count = wbuf.size();
            if (fwrite(&enc_count, sizeof enc_count, 1, out) != 1 ||
                fwrite(wptr, 1, enc_count, out) != enc_count)
                error("io error");
            progress();
        }
    }
    else if (mode == "decode") {
        if (fread(&tmp, sizeof tmp, 1, in) != 1 || tmp != magic)
            error("bad magic");
        while (fread(&enc_count, sizeof enc_count, 1, in)) {
            if (fread(rptr, enc_count, 1, in) != 1)
                error("io error");
            erans_state state;
            state.decode_shrub({rbuf.data(), enc_count});
            raw_count = state.shrub.size();
            wbuf.resize(raw_count);
            state.decode_to({wptr, wbuf.size()});
            if (fwrite(wptr, 1, raw_count, out) != raw_count)
                error("write error");
            progress();
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    auto s = (t1-t0).count()/1e9;
    fprintf(stderr, "total=%6.2fs\n", s);
}
