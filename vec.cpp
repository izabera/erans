#include <utility>
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

#define f(t,lanes,unroll)                                        \
void f_##t##x##lanes##_##unroll(                                 \
        t##x##lanes *__restrict x, t##x##lanes *__restrict y) {  \
    MCA(f_##t##x##lanes##_##unroll,                              \
        for (auto i = 0; i < 256 / lanes; i += unroll) {         \
            unroller.operator()<unroll>([&](auto j) {            \
                if constexpr (std::is_same_v<t,i32>)             \
                    x[i+j] += (y[i+j] - x[i+j]) >> 16;           \
                if constexpr (std::is_same_v<t,f32>)             \
                    x[i+j] += (y[i+j] - x[i+j]) / (1<<16);       \
            });                                                  \
        }                                                        \
    )                                                            \
}

f(i32,8, 1);
f(i32,8, 2);
f(i32,8, 4);
f(i32,8, 8);
f(i32,8,16);
f(i32,8,32);

f(f32,8, 1);
f(f32,8, 2);
f(f32,8, 4);
f(f32,8, 8);
f(f32,8,16);
f(f32,8,32);

#ifdef __AVX512F__
f(i32,16, 1);
f(i32,16, 2);
f(i32,16, 4);
f(i32,16, 8);
f(i32,16,16);

f(f32,16, 1);
f(f32,16, 2);
f(f32,16, 4);
f(f32,16, 8);
f(f32,16,16);
#endif

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
