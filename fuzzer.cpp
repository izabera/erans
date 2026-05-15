// roundtrip fuzzer for erans
//
// two modes:
//   1. libFuzzer harness (build with -DLIBFUZZER and link with -fsanitize=fuzzer):
//      libFuzzer drives LLVMFuzzerTestOneInput with bytes; we roundtrip them.
//   2. standalone (default): runs deterministic edge cases, then loops generating
//      random inputs with assorted distributions until SIGINT or --iters reached.
//
// every input is encoded then decoded and compared byte-for-byte. mismatches abort
// with a diagnostic, so any sanitizer (asan/ubsan) can pin the failure point.

#include "erans.hpp"
#include "types.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct pcg32 {
    u64 state, inc;
    pcg32(u64 seed = 0xc0ffee, u64 stream = 1) : state(0), inc((stream << 1) | 1) {
        gen(); state += seed; gen();
    }
    u32 gen() {
        u64 old = state;
        state = old * 6364136223846793005ull + inc;
        u32 xs  = u32(((old >> 18) ^ old) >> 27);
        u32 rot = u32(old >> 59);
        return (xs >> rot) | (xs << ((-rot) & 31));
    }
    u32 below(u32 n) { return n ? gen() % n : 0; }
    u64 gen64() { return (u64(gen()) << 32) | gen(); }
};

// run one roundtrip; returns true on success, prints + aborts on mismatch.
// on success returns silently so the caller can batch counters.
bool roundtrip(std::string_view in, const char *label) {
    std::string encoded, decoded;
    erans_encode_simple(in, encoded);
    erans_decode_simple(encoded, decoded);

    if (decoded.size() != in.size() ||
        std::memcmp(decoded.data(), in.data(), in.size()) != 0) {

        fprintf(stderr, "\nFUZZ FAILURE [%s] size=%zu enc=%zu dec=%zu\n",
                label, in.size(), encoded.size(), decoded.size());

        // first differing byte
        size_t lim = std::min(in.size(), decoded.size());
        size_t diff = lim;
        for (size_t i = 0; i < lim; i++) {
            if (u8(in[i]) != u8(decoded[i])) { diff = i; break; }
        }
        fprintf(stderr, "  first diff at byte %zu (of %zu)\n", diff, in.size());

        auto dump = [](const char *tag, std::string_view sv, size_t around) {
            size_t lo = around > 8 ? around - 8 : 0;
            size_t hi = std::min(sv.size(), around + 8);
            fprintf(stderr, "  %s[%zu..%zu]:", tag, lo, hi);
            for (size_t i = lo; i < hi; i++)
                fprintf(stderr, " %02x", u8(sv[i]));
            fprintf(stderr, "\n");
        };
        if (diff < lim) {
            dump("in ", in, diff);
            dump("out", decoded, diff);
        }

        // dump short cases entirely so the failure is reproducible by eye
        if (in.size() <= 64) {
            fprintf(stderr, "  full in :");
            for (u8 b : in) fprintf(stderr, " %02x", b);
            fprintf(stderr, "\n  full out:");
            for (char c : decoded) fprintf(stderr, " %02x", u8(c));
            fprintf(stderr, "\n");
        }
        std::abort();
    }
    return true;
}

// ---------- input generators ----------

void gen_uniform(std::vector<u8>& buf, size_t n, pcg32& rng, u32 alphabet_size) {
    buf.resize(n);
    u32 a = std::clamp<u32>(alphabet_size, 1, 256);
    for (size_t i = 0; i < n; i++)
        buf[i] = u8(rng.below(a));
}

void gen_zipf_like(std::vector<u8>& buf, size_t n, pcg32& rng, double skew) {
    // build a cumulative table for 256 symbols with weight ~ 1/(rank+1)^skew
    double total = 0;
    double cdf[257]; cdf[0] = 0;
    for (u32 i = 0; i < 256; i++) {
        total += 1.0 / std::pow(double(i + 1), skew);
        cdf[i + 1] = total;
    }
    // randomly permute symbols so the "hot" rank isn't always byte 0
    u8 perm[256];
    for (u32 i = 0; i < 256; i++) perm[i] = u8(i);
    for (u32 i = 255; i > 0; i--) {
        u32 j = rng.below(i + 1);
        std::swap(perm[i], perm[j]);
    }
    buf.resize(n);
    for (size_t i = 0; i < n; i++) {
        double u = (double(rng.gen()) / 4294967296.0) * total;
        // linear scan is fine, n_symbols is small
        u32 r = 0;
        while (r < 256 && cdf[r + 1] <= u) r++;
        if (r > 255) r = 255;
        buf[i] = perm[r];
    }
}

void gen_runs(std::vector<u8>& buf, size_t n, pcg32& rng, u32 max_run) {
    buf.resize(n);
    size_t i = 0;
    while (i < n) {
        u8 sym = u8(rng.gen());
        u32 run = 1 + rng.below(std::max(1u, max_run));
        size_t end = std::min(n, i + run);
        std::memset(buf.data() + i, sym, end - i);
        i = end;
    }
}

void gen_all256(std::vector<u8>& buf, size_t n, pcg32& rng) {
    // every byte value present at least once, rest random
    buf.resize(std::max<size_t>(n, 256));
    for (u32 i = 0; i < 256; i++) buf[i] = u8(i);
    for (size_t i = 256; i < buf.size(); i++) buf[i] = u8(rng.gen());
    // shuffle
    for (size_t i = buf.size() - 1; i > 0; i--) {
        size_t j = rng.below(u32(i + 1));
        std::swap(buf[i], buf[j]);
    }
    buf.resize(n);
}

// long run of one symbol, then switch to a second symbol -- exercises the f==M
// prefix path described in the readme
void gen_ramp(std::vector<u8>& buf, size_t n, pcg32& rng) {
    buf.resize(n);
    if (n == 0) return;
    u8 a = u8(rng.gen()), b = u8(rng.gen());
    size_t cut = rng.below(u32(n));
    std::memset(buf.data(), a, cut);
    std::memset(buf.data() + cut, b, n - cut);
}

// ---------- edge cases ----------

void run_edge_cases() {
    fprintf(stderr, "edge cases:\n");
    auto rt = [](std::string_view sv, const char *label) {
        roundtrip(sv, label);
        fprintf(stderr, "  ok  %-22s N=%zu\n", label, sv.size());
    };

    rt("",                                     "empty");
    rt("a",                                    "single byte");
    rt("aa",                                   "two same");
    rt("ab",                                   "two different");
    rt(std::string(10,    'x'),                "all-same short");
    rt(std::string(10000, 'x'),                "all-same long");
    rt(std::string(1<<20, 'q'),                "all-same 1Mi");

    // alternating
    {
        std::string s; s.reserve(1000);
        for (int i = 0; i < 500; i++) { s += 'a'; s += 'b'; }
        rt(s, "alternating ab x500");
    }

    // long f==M prefix
    {
        std::string s(50, 'a'); s.append(50, 'b');
        rt(s, "ramp 50a+50b");
    }

    // boundary sizes around renorm thresholds
    for (size_t n : {1u, 2u, 3u, 255u, 256u, 257u, 1023u, 1024u, 1025u,
                     65535u, 65536u, 65537u, 1u<<20, (1u<<24) - 1u, 1u<<24}) {
        std::string s(n, 0);
        for (size_t i = 0; i < n; i++) s[i] = char(i & 0xff);
        char label[64];
        snprintf(label, sizeof label, "ramp 0..255 N=%zu", n);
        rt(s, label);
    }

    // all 256 symbols, each exactly once
    {
        std::string s(256, 0);
        for (u32 i = 0; i < 256; i++) s[i] = char(i);
        rt(s, "0..255 once");
    }

    // every count = 2
    {
        std::string s; s.reserve(512);
        for (u32 i = 0; i < 256; i++) { s += char(i); s += char(i); }
        rt(s, "0..255 twice");
    }

    // every byte value present, but very skewed (one symbol dominates)
    {
        std::string s(10000, 'a');
        for (u32 i = 1; i < 256; i++) s[i * 30 % 10000] = char(i);
        rt(s, "skewed all256 N=10000");
    }
}

// ---------- random loop ----------

std::atomic<bool> stop_flag{false};
void on_sigint(int) { stop_flag = true; }

struct Stats {
    u64 iters = 0;
    u64 bytes_in = 0;
    u64 bytes_out = 0;
    void print(double seconds) const {
        fprintf(stderr,
                "\rran %llu cases  %.2f MB in  %.2f MB out  ratio=%.3f  %.2f MB/s   ",
                (unsigned long long)iters,
                bytes_in / 1e6, bytes_out / 1e6,
                bytes_in > 0 ? double(bytes_out) / double(bytes_in) : 0.0,
                bytes_in / 1e6 / std::max(seconds, 1e-9));
    }
};

void run_random(u64 seed, u64 max_iters, u32 size_cap) {
    fprintf(stderr, "\nrandom fuzz (seed=0x%llx, iters=%llu, size_cap=%u)\n",
            (unsigned long long)seed, (unsigned long long)max_iters, size_cap);

    pcg32 rng(seed);
    std::vector<u8> buf;
    Stats stats;
    auto t0 = std::chrono::steady_clock::now();

    char label[64];
    while (!stop_flag && (max_iters == 0 || stats.iters < max_iters)) {
        // pick a size with a log-uniform-ish distribution so we exercise both
        // small inputs (lots of edge interactions) and large ones (renorm paths)
        u32 cap = std::min<u32>(size_cap, erans_maxsize);
        u32 size_bits = rng.below(25); // 0..24 -> sizes up to ~16Mi
        u32 n;
        if (size_bits == 0) {
            n = rng.below(16);
        } else {
            u32 hi = std::min<u32>(cap, 1u << size_bits);
            u32 lo = (size_bits == 1) ? 1 : (1u << (size_bits - 1));
            n = lo + rng.below(hi - lo + 1);
        }
        if (n > cap) n = cap;

        u32 shape = rng.below(7);
        switch (shape) {
            case 0:
                gen_uniform(buf, n, rng, 256);
                snprintf(label, sizeof label, "uniform-256 N=%u", n);
                break;
            case 1: {
                u32 a = 1 + rng.below(16);
                gen_uniform(buf, n, rng, a);
                snprintf(label, sizeof label, "uniform-%u N=%u", a, n);
                break;
            }
            case 2: {
                double skews[] = {0.5, 1.0, 1.5, 2.5};
                double s = skews[rng.below(4)];
                gen_zipf_like(buf, n, rng, s);
                snprintf(label, sizeof label, "zipf s=%.1f N=%u", s, n);
                break;
            }
            case 3: {
                u32 mr = 1 + rng.below(std::max(1u, n));
                gen_runs(buf, n, rng, mr);
                snprintf(label, sizeof label, "runs<=%u N=%u", mr, n);
                break;
            }
            case 4:
                gen_all256(buf, n, rng);
                snprintf(label, sizeof label, "all256 N=%u", n);
                break;
            case 5:
                gen_ramp(buf, n, rng);
                snprintf(label, sizeof label, "ramp N=%u", n);
                break;
            case 6: {
                // 1 or 2 distinct bytes only
                buf.resize(n);
                u8 a = u8(rng.gen()), b = u8(rng.gen());
                for (u32 i = 0; i < n; i++) buf[i] = (rng.gen() & 1) ? a : b;
                snprintf(label, sizeof label, "binary {%02x,%02x} N=%u", a, b, n);
                break;
            }
        }

        std::string_view sv(reinterpret_cast<const char*>(buf.data()), buf.size());
        roundtrip(sv, label);

        // also account encoded size cheaply (re-encode is wasteful; do it once here)
        // we already encoded inside roundtrip, but roundtrip doesn't return it.
        // re-encoding for accounting only on a sampled subset to keep speed up.
        if ((stats.iters & 0x1f) == 0) {
            std::string e;
            erans_encode_simple(sv, e);
            stats.bytes_out += e.size();
            stats.bytes_in  += sv.size();
        }

        stats.iters++;
        if ((stats.iters & 0xff) == 0) {
            auto t1 = std::chrono::steady_clock::now();
            stats.print((t1 - t0).count() / 1e9);
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    stats.print((t1 - t0).count() / 1e9);
    fprintf(stderr, "\ndone (%s)\n", stop_flag ? "interrupted" : "iters reached");
}

} // anon

#ifdef LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > erans_maxsize) size = erans_maxsize;
    std::string_view sv(reinterpret_cast<const char*>(data), size);
    roundtrip(sv, "libfuzzer");
    return 0;
}
#else

static void usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s [--seed N] [--iters N] [--size-cap N] [--skip-edge]\n"
        "  --seed N      PRNG seed (default: time-based)\n"
        "  --iters N     stop after N random cases (default: 0 = infinite)\n"
        "  --size-cap N  cap generated input size (default: %u = erans_maxsize)\n"
        "  --skip-edge   skip the deterministic edge-case suite\n"
        "  --only-edge   run edge cases and exit\n",
        argv0, u32(erans_maxsize));
}

int main(int argc, char **argv) {
    u64 seed = u64(std::chrono::steady_clock::now().time_since_epoch().count());
    u64 iters = 0;
    u32 size_cap = erans_maxsize;
    bool skip_edge = false, only_edge = false;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto need = [&](int more) {
            if (i + more >= argc) { usage(argv[0]); std::exit(2); }
        };
        if      (a == "--seed")      { need(1); seed     = std::strtoull(argv[++i], nullptr, 0); }
        else if (a == "--iters")     { need(1); iters    = std::strtoull(argv[++i], nullptr, 0); }
        else if (a == "--size-cap")  { need(1); size_cap = u32(std::strtoul (argv[++i], nullptr, 0)); }
        else if (a == "--skip-edge") { skip_edge = true; }
        else if (a == "--only-edge") { only_edge = true; }
        else if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else { fprintf(stderr, "unknown arg: %s\n", argv[i]); usage(argv[0]); return 2; }
    }

    std::signal(SIGINT, on_sigint);

    if (!skip_edge) run_edge_cases();
    if (only_edge) return 0;

    run_random(seed, iters, size_cap);
    return 0;
}

#endif
