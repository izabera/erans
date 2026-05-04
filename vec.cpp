#include <utility>
#include <type_traits>
#include "types.hpp"

#define MCA(name, ...) \
    asm volatile("# LLVM-MCA-BEGIN " #name); \
    __VA_ARGS__ \
    asm volatile("# LLVM-MCA-END");

auto unroller = []<int Unroll>(auto lambda) {
    []<std::size_t... idx> (auto lambda, std::index_sequence<idx...>) {
        (lambda(idx), ...);
    }(lambda, std::make_index_sequence<Unroll>{});
};

template <typename T> constexpr int shift_for = 4 * sizeof(T);

#define f(t,lanes,unroll)                                                  \
void f_##t##x##lanes##_##unroll(                                           \
        t##x##lanes *__restrict x, t##x##lanes *__restrict y) {            \
    constexpr int sh = shift_for<t>;                                       \
    MCA(f_##t##x##lanes##_##unroll,                                        \
        for (auto i = 0; i < 256 / lanes; i += unroll) {                   \
            unroller.operator()<unroll>([&](auto j) {                      \
                if constexpr (std::is_integral_v<t>)                       \
                    x[i+j] += (y[i+j] - x[i+j]) >> sh;                     \
                else                                                       \
                    x[i+j] += (y[i+j] - x[i+j]) / (1 << sh);               \
            });                                                            \
        }                                                                  \
    )                                                                      \
}

// f(i32,8, 1);
// f(i32,8, 2);
// f(i32,8, 4);
// f(i32,8, 8);
// f(i32,8,16);
f(i32,8,32);

// f(f32,8, 1);
// f(f32,8, 2);
// f(f32,8, 4);
// f(f32,8, 8);
// f(f32,8,16);
f(f32,8,32);

// #ifdef __AVX2__
// f(i16,16, 1);
// f(i16,16, 2);
// f(i16,16, 4);
// f(i16,16, 8);
f(i16,16,16);
// #endif

// #ifdef __AVX512F__
// f(i32,16, 1);
// f(i32,16, 2);
// f(i32,16, 4);
// f(i32,16, 8);
f(i32,16,16);

// f(f32,16, 1);
// f(f32,16, 2);
// f(f32,16, 4);
// f(f32,16, 8);
f(f32,16,16);
// #endif

// #ifdef __AVX512BW__
// f(i16,32, 1);
// f(i16,32, 2);
// f(i16,32, 4);
f(i16,32, 8);
// #endif

#if 1
// #ifdef __AVX512FP16__
// #ifdef __AVX512VL__
// f(f16,16, 1);
// f(f16,16, 2);
// f(f16,16, 4);
// f(f16,16, 8);
f(f16,16,16);
// #endif
// f(f16,32, 1);
// f(f16,32, 2);
// f(f16,32, 4);
f(f16,32, 8);
// #endif
#endif

#if 0
// #ifdef __AVX10_2__
// f(bf16,16, 1);
// f(bf16,16, 2);
// f(bf16,16, 4);
// f(bf16,16, 8);
f(bf16,16,16);

// f(bf16,32, 1);
// f(bf16,32, 2);
// f(bf16,32, 4);
f(bf16,32, 8);
// #endif
#endif

#if 0
void f_i32(i32 *__restrict x, i32 *__restrict y) {
    MCA(f_i32,
        for (auto i = 0; i < 256; i++)
             x[i] += (y[i] - x[i]) >> 16;
    )
}

void f_f32(f32 *__restrict x, f32 *__restrict y) {
    MCA(f_f32,
        for (auto i = 0; i < 256; i++)
             x[i] += (y[i] - x[i]) / (1<<16);
    )
}

void f_i16(i16 *__restrict x, i16 *__restrict y) {
    MCA(f_i16,
        for (auto i = 0; i < 256; i++)
             x[i] += (y[i] - x[i]) >> 8;
    )
}

// void f_f16(f16 *__restrict x, f16 *__restrict y) {
//     MCA(f_f16,
//         for (auto i = 0; i < 256; i++)
//              x[i] += (y[i] - x[i]) / (1<<8);
//     )
// }

// #ifdef __AVX10_2__
// void f_bf16(bf16 *__restrict x, bf16 *__restrict y) {
//     MCA(f_bf16,
//         for (auto i = 0; i < 256; i++)
//              x[i] += (y[i] - x[i]) / (1<<8);
//     )
// }
// #endif

#endif
