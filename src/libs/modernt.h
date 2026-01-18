#pragma once

#include <stdint.h>

// Integer types
typedef char c8;
typedef int8_t i8;
typedef uint8_t u8;
typedef int16_t i16;
typedef uint16_t u16;
typedef int32_t i32;
typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;

// Pointer-sized types
typedef intptr_t isize;
typedef uintptr_t usize;

// Float types (if you want full Rust parity)
typedef float f32;
typedef double f64;

// Literal macros
#define U8(c)  UINT8_C (c)
#define U16(c) UINT16_C (c)
#define U32(c) UINT32_C (c)
#define U64(c) UINT64_C (c)

// Max value constants
#define U8_MAX  UINT8_MAX
#define U16_MAX UINT16_MAX
#define U32_MAX UINT32_MAX
#define U64_MAX UINT64_MAX
#define I8_MAX  INT8_MAX
#define I16_MAX INT16_MAX
#define I32_MAX INT32_MAX
#define I64_MAX INT64_MAX

// Compile-time size assertions
// NOLINTBEGIN(readability-magic-numbers)
static_assert (sizeof (u8) == 1, "u8 is not 1 byte");
static_assert (sizeof (u16) == 2, "u16 is not 2 bytes");
static_assert (sizeof (u32) == 4, "u32 is not 4 bytes");
static_assert (sizeof (u64) == 8, "u64 is not 8 bytes");
static_assert (sizeof (usize) == sizeof (void*), "usize differs from pointer size");
static_assert (sizeof (isize) == sizeof (void*), "isize differs from pointer size");
// NOLINTEND(readability-magic-numbers)
