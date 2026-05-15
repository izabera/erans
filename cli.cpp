#include "erans.hpp"
#include "fse_wrapper.hpp"
#include "hts_wrapper.hpp"
#include "nayuki.hpp"
#include "ryg_wrapper.hpp"
#include "utils.hpp"
#include "types.hpp"
#include <cstdio>
#include <chrono>
#include <exception>
#include <span>
#include <string>
#include <string_view>

enum class Codec {
    erans,
    fse,
    nayuki_static,
    nayuki_adaptive,
    hts_rans4x16,
    ryg_rans64,
};

struct CodecSpec {
    Codec codec;
    const char *name;
};

static CodecSpec parse_codec(std::string_view name, const char *usage) {
    if (name == "erans")
        return {Codec::erans, "erans"};
    if (name == "fse")
        return {Codec::fse, "fse"};
    if (name == "nayuki-static")
        return {Codec::nayuki_static, "nayuki-static"};
    if (name == "nayuki-adaptive")
        return {Codec::nayuki_adaptive, "nayuki-adaptive"};
    if (name == "hts-rans4x16")
        return {Codec::hts_rans4x16, "hts-rans4x16"};
    if (name == "ryg-rans64")
        return {Codec::ryg_rans64, "ryg-rans64"};
    error(usage);
}

static void encode_block(Codec codec, std::string_view in, std::string& out) {
    switch (codec) {
    case Codec::erans:
        erans_encode_simple(in, out);
        return;
    case Codec::fse:
        fse_encode(in, out);
        return;
    case Codec::nayuki_static:
        nayuki_static_encode(in, out);
        return;
    case Codec::nayuki_adaptive:
        nayuki_adaptive_encode(in, out);
        return;
    case Codec::hts_rans4x16:
        hts_rans4x16_encode(in, out);
        return;
    case Codec::ryg_rans64:
        ryg_rans64_encode(in, out);
        return;
    }
    error("unknown codec");
}

static void decode_block(Codec codec, std::string_view in, std::string& out) {
    switch (codec) {
    case Codec::erans:
        erans_decode_simple(in, out);
        return;
    case Codec::fse:
        fse_decode(in, out);
        return;
    case Codec::nayuki_static:
        nayuki_static_decode(in, out);
        return;
    case Codec::nayuki_adaptive:
        nayuki_adaptive_decode(in, out);
        return;
    case Codec::hts_rans4x16:
        hts_rans4x16_decode(in, out);
        return;
    case Codec::ryg_rans64:
        ryg_rans64_decode(in, out);
        return;
    }
    error("unknown codec");
}

int main(int argc, char **argv) {
    auto usage = "usage:\n"
                 "    erans-cli [--codec erans|fse|nayuki-static|nayuki-adaptive|hts-rans4x16|ryg-rans64] encode [infile [outfile]]\n"
                 "    erans-cli [--codec erans|fse|nayuki-static|nayuki-adaptive|hts-rans4x16|ryg-rans64] decode [infile [outfile]]\n"
               //"    erans-cli info   [file]\n"
                 ;

    if (argc < 2)
        error(usage);

    CodecSpec codec = {Codec::erans, "erans"};
    std::string mode;
    const char *files[2] = {};
    int file_count = 0;

    for (int i = 1; i < argc; i++) {
        std::string_view arg(argv[i]);
        if (arg == "encode" || arg == "decode") {
            if (!mode.empty())
                error(usage);
            mode = std::string(arg);
        }
        else if (arg == "--codec" || arg == "-c") {
            if (++i == argc)
                error(usage);
            codec = parse_codec(argv[i], usage);
        }
        else if (arg.starts_with("--codec=")) {
            codec = parse_codec(arg.substr(8), usage);
        }
        else if (arg == "--help" || arg == "-h") {
            fputs(usage, stderr);
            return 0;
        }
        else {
            if (file_count == 2)
                error(usage);
            files[file_count++] = argv[i];
        }
    }

    if (mode != "encode" && mode != "decode")
        error(usage);

    auto in  = file_count > 0 ? fopen(files[0], "rb") : stdin;
    auto out = file_count > 1 ? fopen(files[1], "wb") : stdout;
    if (!in || !out)
        error("could not open file");

    std::string rbuf(erans_maxsize*2, '\0'), wbuf;
    wbuf.reserve(erans_maxsize*2);
    auto rptr = reinterpret_cast<u8*>(rbuf.data());

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
        fprintf(stderr, "\r%s/%s: raw=%7.2fMB - %6.2fMB/s   enc=%7.2fMB - %6.2f MB/s   ",
                mode.data(),
                codec.name,
                raw_total/1e6, (raw_total/1e6) / s,
                enc_total/1e6, (enc_total/1e6) / s);
    };

    try {
        if (mode == "encode") {
            if (fwrite(&magic, sizeof magic, 1, out) != 1)
                error("io error");
            while ((raw_count = fread(rptr, 1, erans_maxsize, in))) {
                encode_block(codec.codec, {rbuf.data(), raw_count}, wbuf);
                enc_count = wbuf.size();
                auto wptr = reinterpret_cast<const u8*>(wbuf.data());
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
                if (enc_count > rbuf.size()) {
                    rbuf.resize(enc_count);
                    rptr = reinterpret_cast<u8*>(rbuf.data());
                }
                if (fread(rptr, enc_count, 1, in) != 1)
                    error("io error");
                decode_block(codec.codec, {rbuf.data(), enc_count}, wbuf);
                raw_count = wbuf.size();
                auto wptr = reinterpret_cast<const u8*>(wbuf.data());
                if (fwrite(wptr, 1, raw_count, out) != raw_count)
                    error("write error");
                progress();
            }
        }
    } catch (const std::exception& e) {
        error(e.what());
    }

    auto t1 = std::chrono::steady_clock::now();
    auto s = (t1-t0).count()/1e9;
    fprintf(stderr, "total=%6.2fs\n", s);
}
