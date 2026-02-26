#pragma once

// it is meant to mean not-rust and no-trust, since
// adding the pointer `*` backed into the typedef is
// not safe when people are lazy to write 2 times the
// type, making `cstr a, b, c`
// since cstr is `const u8*`, a is `const u8*`
// and all others are just `u8`
// not safe for some people

#include <stdint.h>

typedef char c8;
typedef int8_t i8;
typedef uint8_t u8;
typedef int16_t i16;
typedef uint16_t u16;
typedef int32_t i32;
typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;

// In macros using literal insert (`print_##type`)
//
// `print_u8*` -> `print_cstr`
typedef const u8* cstr;
typedef u8* ustr;
// natural C "string", warnings about immutable use `-Werror`,
typedef const c8* nstr;
typedef c8* rstr;

typedef intptr_t isize;
typedef uintptr_t usize;

typedef float f32;
typedef double f64;

#define U8(c)  UINT8_C (c)
#define U16(c) UINT16_C (c)
#define U32(c) UINT32_C (c)
#define U64(c) UINT64_C (c)

#define U8_MAX  UINT8_MAX
#define U16_MAX UINT16_MAX
#define U32_MAX UINT32_MAX
#define U64_MAX UINT64_MAX
#define I8_MAX  INT8_MAX
#define I16_MAX INT16_MAX
#define I32_MAX INT32_MAX
#define I64_MAX INT64_MAX
#define I8_MIN  INT8_MIN
#define I16_MIN INT16_MIN
#define I32_MIN INT32_MIN
#define I64_MIN INT64_MIN

// NOLINTBEGIN(readability-magic-numbers)
static_assert (sizeof (u8) == 1, "u8 is not 1 byte");
static_assert (sizeof (u16) == 2, "u16 is not 2 bytes");
static_assert (sizeof (u32) == 4, "u32 is not 4 bytes");
static_assert (sizeof (u64) == 8, "u64 is not 8 bytes");
static_assert (sizeof (f32) == 4, "f32 is not 4 bytes");
static_assert (sizeof (f64) == 8, "f64 is not 8 bytes");
static_assert (sizeof (usize) == sizeof (void*), "usize differs from pointer size");
static_assert (sizeof (isize) == sizeof (void*), "isize differs from pointer size");
// NOLINTEND(readability-magic-numbers)
