#pragma once
#include <immintrin.h>
#include <cstdint>

using i8  = int8_t ; using u8  = uint8_t ;
using i16 = int16_t; using u16 = uint16_t; using f16 = _Float16; using bf16 = __bf16;
using i32 = int32_t; using u32 = uint32_t; using f32 = _Float32;
using i64 = int64_t; using u64 = uint64_t; using f64 = _Float64;

template <typename t, int n>
using simd __attribute__((vector_size(sizeof(t)*n),aligned(sizeof(t)*n))) = t;

using i8x4   = simd<i8 , 4>; using u8x4   = simd<u8 , 4>;
using i16x4  = simd<i16, 4>; using u16x4  = simd<u16, 4>; using f16x4  = simd<f16, 4>; using bf16x4  = simd<bf16, 4>;
using i32x4  = simd<i32, 4>; using u32x4  = simd<u32, 4>; using f32x4  = simd<f32, 4>;
using i64x4  = simd<i64, 4>; using u64x4  = simd<u64, 4>; using f64x4  = simd<f64, 4>;

using i8x8   = simd<i8 , 8>; using u8x8   = simd<u8 , 8>;
using i16x8  = simd<i16, 8>; using u16x8  = simd<u16, 8>; using f16x8  = simd<f16, 8>; using bf16x8  = simd<bf16, 8>;
using i32x8  = simd<i32, 8>; using u32x8  = simd<u32, 8>; using f32x8  = simd<f32, 8>;
using i64x8  = simd<i64, 8>; using u64x8  = simd<u64, 8>; using f64x8  = simd<f64, 8>;

using i8x16  = simd<i8 ,16>; using u8x16  = simd<u8 ,16>;
using i16x16 = simd<i16,16>; using u16x16 = simd<u16,16>; using f16x16 = simd<f16,16>; using bf16x16 = simd<bf16,16>;
using i32x16 = simd<i32,16>; using u32x16 = simd<u32,16>; using f32x16 = simd<f32,16>;
using i64x16 = simd<i64,16>; using u64x16 = simd<u64,16>; using f64x16 = simd<f64,16>;

using i8x32  = simd<i8 ,32>; using u8x32  = simd<u8 ,32>;
using i16x32 = simd<i16,32>; using u16x32 = simd<u16,32>; using f16x32 = simd<f16,32>; using bf16x32 = simd<bf16,32>;
using i32x32 = simd<i32,32>; using u32x32 = simd<u32,32>; using f32x32 = simd<f32,32>;
using i64x32 = simd<i64,32>; using u64x32 = simd<u64,32>; using f64x32 = simd<f64,32>;

using i8x64  = simd<i8 ,64>; using u8x64  = simd<u8 ,64>;
using i16x64 = simd<i16,64>; using u16x64 = simd<u16,64>; using f16x64 = simd<f16,64>; using bf16x64 = simd<bf16,64>;
using i32x64 = simd<i32,64>; using u32x64 = simd<u32,64>; using f32x64 = simd<f32,64>;
using i64x64 = simd<i64,64>; using u64x64 = simd<u64,64>; using f64x64 = simd<f64,64>;
