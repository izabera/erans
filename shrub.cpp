#include "shrub.hpp"
#include <bit>
#include <iostream>
#include <cstdio>

void Shrub::debug() const {
    auto show = [](auto &vec, auto prefix) {
        std::cout << prefix << ' ';
        for (auto i = 0; i < 16; i++)
            std::cout << vec[i] << ' ';
        std::cout << std::endl;
    };
    show(top, "top:       ");
    for (auto i = 0; i < 16; i++) {
        char prefix[20];
        sprintf(prefix, "bottom[%2d]:", i);
        show(bottom[i], prefix);
    }
};



// probably the slowest implementation of rice of all time?
// it's all very sequential but at least some parts could be vectorised
// also you can probably do more than one byte at a time
// todo: check how other people do it
template <u32 k>
struct rice {
    static constexpr u64 mask = (1 << k) - 1;

    constexpr static u8* enc_binary(u32 *__restrict vals, u8 *__restrict bytes) {
        u64 buf = 0, bits = 0;
        for (u32 i = 0; i < 256; i++) {
            buf |= u64(vals[i] & mask) << bits;
            bits += k;
            while (bits >= 8) {
                *bytes++ = u8(buf);
                buf >>= 8;
                bits -= 8;
            }
        }
        // no trailing partial byte because 256 is a multiple of 8
        return bytes;
    }

    constexpr static u8* dec_binary(u32 *__restrict vals, u8 *__restrict bytes) {
        u64 buf = 0, bits = 0;
        for (u32 i = 0; i < 256; i++) {
            while (bits < k) {
                buf |= u64(*bytes++) << bits;
                bits += 8;
            }
            vals[i] |= u32(buf & mask);
            buf >>= k;
            bits -= k;
        }
        return bytes;
    }

    constexpr static u8* enc_unary(u32 *__restrict vals, u8 *__restrict bytes) {
        u64 buf = 0, bits = 0;
        for (u32 i = 0; i < 256; i++) {
            u32 q = vals[i] >> k;

            // add q zeros (q can be > 64)
            // the high bits of buf are already 0, so each flush here
            // just adds (8 - bits) zeros from the run for free
            while (q >= 8 - bits) {
                *bytes++ = u8(buf);
                buf >>= 8;
                q -= 8 - bits;
                bits = 0;
            }
            bits += q;

            // followed by a 1
            buf |= u64(1) << bits;
            if (++bits == 8) {
                *bytes++ = u8(buf);
                buf = bits = 0;
            }
        }
        if (bits) *bytes++ = u8(buf);
        return bytes;
    }

    constexpr static u8* dec_unary(u32 *__restrict vals, u8 *__restrict bytes) {
        u64 buf = 0, bits = 0;
        for (u32 i = 0; i < 256; i++) {
            u32 q = 0;
            while (buf == 0) {
                q += bits;
                buf = *bytes++;
                bits = 8;
            }

            u32 tz = __builtin_ctz(buf); // buf has at most 1 byte
            q += tz;
            buf >>= tz + 1;
            bits -= tz + 1;
            vals[i] |= q << k;
        }
        return bytes;
    }
};



#define dispatch(f)                                                         \
constexpr static u8* f(u32 k, u32 *__restrict vals, u8 *__restrict bytes) { \
    switch (k) {                                                            \
        case  0: return rice< 0>::f(vals, bytes);                           \
        case  1: return rice< 1>::f(vals, bytes);                           \
        case  2: return rice< 2>::f(vals, bytes);                           \
        case  3: return rice< 3>::f(vals, bytes);                           \
        case  4: return rice< 4>::f(vals, bytes);                           \
        case  5: return rice< 5>::f(vals, bytes);                           \
        case  6: return rice< 6>::f(vals, bytes);                           \
        case  7: return rice< 7>::f(vals, bytes);                           \
        case  8: return rice< 8>::f(vals, bytes);                           \
        case  9: return rice< 9>::f(vals, bytes);                           \
        case 10: return rice<10>::f(vals, bytes);                           \
        case 11: return rice<11>::f(vals, bytes);                           \
        case 12: return rice<12>::f(vals, bytes);                           \
        case 13: return rice<13>::f(vals, bytes);                           \
        case 14: return rice<14>::f(vals, bytes);                           \
        case 15: return rice<15>::f(vals, bytes);                           \
        case 16: return rice<16>::f(vals, bytes);                           \
        default: __builtin_unreachable();                                   \
    }                                                                       \
}

dispatch(enc_binary)
dispatch(dec_binary)
dispatch(enc_unary)
dispatch(dec_unary)



//  8b    256 * x       256 * y
// [k] [rice-binary] [rice-unary]
// the rice param k maxes out at 16, and higher values can be used as sentinels
u8* Shrub::encode(u8 *bytes) {
    u32 total = 0;
    for (auto c : counts)
        total += c;

    // optimal k is floor(log2(avg)) but this is simple enough
    u32 k = 0;
    u32 best_bits = total; // k = 0  ==  everything in unary
    for (u32 candidate = std::max(0, std::bit_width(total)-8); candidate > 0; candidate++) {
        u32 bits = 256 * candidate;
        for (auto c : counts)
            bits += c >> candidate;
        if (bits < best_bits) {
            best_bits = bits;
            k = candidate;
        }
        else break; // at the 2nd iteration at most, i think?
    }

    *bytes++ = k;
    bytes = enc_binary(k, counts, bytes);
    bytes = enc_unary (k, counts, bytes);

    return bytes;
}

u8* Shrub::decode(u8 *bytes) {
    u32 k = *bytes++;
    bytes = dec_binary(k, counts, bytes);
    bytes = dec_unary (k, counts, bytes);

    // rebuild shrub state
    u32 c = 0;
    for (u32 g = 0; g < 16; g++) {
        top[g] = c;
        u32 g_cum = 0;
        for (u32 l = 0; l < 16; l++) {
            u8 s = (g << 4) | l;
            bottom[g][l] = g_cum;
            g_cum += counts[s];
        }
        c += g_cum;
    }
    return bytes;
}



#if RUNTIME_TESTS
#include <cstdio>
#define LOG(...) printf(__VA_ARGS__)
#else
#define LOG(...)
#endif

// XXX: this doesn't compile because k=0 has too many iterations in unary
#ifdef COMPTIME_TESTS
constexpr
#endif
static bool check_rice() {
    constexpr auto max = 24*1024*1024;

    u32 vals[256]{};
    u32 decoded[256]{};
    u8 buffer[(max+256)/8]{}; // worst case is 2^24 + 256 bits = just over 2mb

    struct pcg32 {
        u64 state = 987654321, inc = 1234567;

        constexpr u32 gen() {
            auto old = state;
            state = old * 6364136223846793005ull + inc;
            u32 xs = ((old >> 18) ^ old) >> 27;
            u32 rot = old >> 59;
            return (xs >> rot) | (xs << ((-rot)&31));
        }
    } rng;

    auto test_k = [&](u32 k) {
        auto budget = max;

        // fill with random values that sum to 2^24
        for (auto i = 0; i < 255; i++) {
            auto v = rng.gen();
            while (v > budget)
                v >>= 1;
            vals[i] = v;
            budget -= v;
            decoded[i] = 0;
        }
        vals[255] = budget;

        u8 *enc, *dec;
        enc = enc_binary(k, vals, buffer);
        enc = enc_unary (k, vals, enc);

        dec = dec_binary(k, decoded, buffer);
        dec = dec_unary (k, decoded, dec);

        if (enc != dec) {
            LOG("mismatch enc=%p dec=%p\n", enc, dec);
            return false;
        }
        for (auto i = 0; i < 256; i++) {
            if (decoded[i] != vals[i]) {
                LOG("mismatch at pos %d: vals=%u decoded=%u\n", i, vals[i], decoded[i]);
                return false;
            }
        }
        return true;
    };

    for (u32 k = 0; k <= 16; k++) {
        LOG("check k=%u\n", k);
        if (!test_k(k)) return false;
    }

    LOG("pass\n");
    return true;
}

#ifdef COMPTIME_TESTS
static_assert(check_rice());
#elif RUNTIME_TESTS
int main() { check_rice(); }
#endif
