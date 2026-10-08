// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

/// @file notrust.h
/// @brief Fixed-width integer, float, and byte-string type aliases.
///
/// Provides short-form fixed-width types (u8..u64, i8..i64, f32, f64),
/// size-matched isize/usize, min/max/cast macros, and byte-string
/// (zstr/czstr) and native-string (nstr/cnstr) typedefs, backed by
/// compiler builtin type macros and validated with static_assert.
///
/// @note Requires GCC or Clang. No MSVC support.
#pragma once

#ifndef __CHAR_BIT__
#error This header requires compiler to know about the machine it targets
#endif

#if !defined(__GNUC__) && !defined(__clang__)
#ifdef _MSC_VER
#error This header requires GCC or Clang, and does not support MSVC
#else
#error This header requires GCC or Clang
#endif
#endif


// clang-format off
typedef char              c8;
typedef __INT8_TYPE__     i8;
typedef __UINT8_TYPE__    u8;
typedef __INT16_TYPE__    i16;
typedef __UINT16_TYPE__   u16;
typedef __INT32_TYPE__    i32;
typedef __UINT32_TYPE__   u32;
typedef __INT64_TYPE__    i64;
typedef __UINT64_TYPE__   u64;
typedef __PTRDIFF_TYPE__  isize;
typedef __SIZE_TYPE__     usize;
// clang-format on

typedef float  f32;
typedef double f64;

#define U8_MAX  __UINT8_MAX__
#define U16_MAX __UINT16_MAX__
#define U32_MAX __UINT32_MAX__
#define U64_MAX __UINT64_MAX__

#define I8_MAX  __INT8_MAX__
#define I16_MAX __INT16_MAX__
#define I32_MAX __INT32_MAX__
#define I64_MAX __INT64_MAX__

#define I8_MIN  (-__INT8_MAX__ - 1)
#define I16_MIN (-__INT16_MAX__ - 1)
#define I32_MIN (-__INT32_MAX__ - 1)
#define I64_MIN (-__INT64_MAX__ - 1)

/// byte string, 0b00000000 == 0 (mutable)
typedef u8* zstr;
/// byte string, 0b00000000 == 0 (immutable)
typedef const u8* czstr;
/// native/libc-facing char string
typedef c8* nstr;
/// native/libc-facing char string (const)
typedef const c8* cnstr;

// NOLINTBEGIN(readability-magic-numbers)
// clang-format off
static_assert (__CHAR_BIT__ == 8, "byte is not 8 bits, this codebase assumes octets");
static_assert (sizeof (u8) == 1, "u8 is not 1 byte");
static_assert (sizeof (u16) == 2, "u16 is not 2 bytes");
static_assert (sizeof (u32) == 4, "u32 is not 4 bytes");
static_assert (sizeof (u64) == 8, "u64 is not 8 bytes");
static_assert (sizeof (f32) == 4, "f32 is not 4 bytes");
static_assert (sizeof (f64) == 8, "f64 is not 8 bytes");
static_assert (sizeof (usize) == sizeof (void*), "usize differs from pointer size");
static_assert (sizeof (isize) == sizeof (void*), "isize differs from pointer size");
// clang-format on
// NOLINTEND(readability-magic-numbers)
