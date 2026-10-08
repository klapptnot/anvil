// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

/// @file z3_toys.h
/// @brief Core utility macros and functions for common operations.
///
/// Provides error-printing and fatal-exit macros, qualifier-discard
/// suppression, buffer-popping helpers, and alignment utilities.
///
/// @note Requires C23 (`-std=c23`).
#pragma once

#ifndef __STDC_VERSION__
#error A modern C standard (like C23) is required
#elif __STDC_VERSION__ < 202311L
#error This code must be compiled with -std=c23
#endif

#include <notrust.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// clang-format off
#if defined(__GNUC__) || defined(__clang__)
/// @brief Suppress discarding qualifier warnings around a
/// declaration/statement.
/// @param decl The declaration or statement to wrap.
#define Z3_DISCARD_QUAL(decl)                                    \
  _Pragma ("GCC diagnostic push")                                \
    _Pragma ("GCC diagnostic ignored \"-Wcast-qual\"") _Pragma ( \
      "GCC diagnostic ignored \"-Wincompatible-pointer-types-discards-qualifiers\""     \
    ) decl _Pragma ("GCC diagnostic pop")
#else
/// @brief No-op fallback; suppression isn't available
/// @param decl The declaration or statement to wrap.
#define Z3_DISCARD_QUAL(decl) decl
#endif
// clang-format on

#define Z3_CONCAT_(a, b) a##b
#define Z3_CONCAT(a, b)  Z3_CONCAT_ (a, b)

/// @brief Print a formatted message to stderr, without color or an [ERROR] tag.
///
/// Just saves having to write `stderr` and the NOLINT for the return value
/// every time.
///
/// @param fmt printf-style format string.
/// @param ... Arguments matching `fmt`, if any.
#define eprintf(fmt, ...) \
  (void)fprintf (stderr, fmt __VA_OPT__ (, ) __VA_ARGS__)

/// @brief Print a formatted error message to stderr in red.
/// @param fmt printf-style format string.
/// @param ... Arguments matching `fmt`, if any.
#define errpfmt(fmt, ...) \
  eprintf ("\x1b[38;5;9m[ERROR] " fmt "\x1b[0m\n", __VA_ARGS__)

/// @brief Print a formatted error message and terminate the process.
///
/// @param fmt printf-style format string.
/// @param ... Arguments matching `fmt`, if any.
#define die(fmt, ...)                             \
  {                                               \
    errpfmt (fmt, __VA_ARGS__);                   \
    exit (1); /* NOLINT(concurrency-mt-unsafe) */ \
  }

/// @brief Pop the next value off a `(count, pointer)` pair, advancing the
/// pointer.
///
/// On success decrements @p c, dereferences @p v, and advances @p v past
/// the value. If @p c is exhausted, prints an error and exits the process.
///
/// @param c Remaining-count variable (lvalue), decremented on success.
/// @param v Pointer variable (lvalue) into the buffer, advanced on success.
/// @return The popped value, or `(typeof(*v))0` on the (unreachable, due
///         to `exit`) failure path.
#define popf(c, v)                                                    \
  (c > 0 ? (--c, *v++)                                                \
         : (errpfmt ("Trying to access a non-existent value"),      \
             exit (EXIT_FAILURE) /* NOLINT(concurrency-mt-unsafe) */, \
             (typeof (*v))0))

/// @brief Return @p ret_val from the enclosing function if @p expr is negative.
/// @param expr Expression to evaluate; treated as failure if `< 0`.
/// @param ret_val Value to return on failure.
#define CHECK_OR_RETURN(expr, ret_val) \
  if ((expr) < 0) return (ret_val)

/// @brief Exit the process via `perror`/`exit(EXIT_FAILURE)` if @p expr is
/// negative.
/// @param expr Expression to evaluate; treated as failure if `< 0`.
/// @param msg Message passed to `perror()` on failure.
#define CHECK_OR_EXIT(expr, msg) \
  if ((expr) < 0) {              \
    perror (msg);                \
    exit (EXIT_FAILURE);         \
  }

/// @brief Calculate the next power of 2 greater than or equal to @p n
/// @param n Lower bound value
/// @return The smallest power of 2 that is `>= n`
usize z3_powtwo_ceil (usize n);

/// @brief Round @p n up to the nearest multiple of `sizeof(void*)`
/// @param n Lower bound value
/// @return The smallest multiple of `sizeof(void*)` that is `>= n`
[[clang::always_inline, maybe_unused]]
static inline usize z3_usize_ceil_align (usize n) {
  usize aligned = (n + (sizeof (void*) - 1)) & ~(sizeof (void*) - 1);
  if (aligned < n)
    die ("z3_usize_ceil_align: overflow, cannot align to pointer width");
  return aligned;
}

/// @brief Round @p n up to the nearest multiple of @p align
/// @param n Lower bound value
/// @param align Expected alignment
/// @return The smallest multiple of @p align that is `>= n`
[[clang::always_inline, maybe_unused]]
static inline usize z3_align_to (usize n, usize align) {
  usize aligned = (n + (align - 1)) & ~(align - 1);
  if (aligned < n) die ("z3_align_to: overflow, cannot align to boundary");
  return aligned;
}

#ifdef Z3_TOYS_IMPL
usize z3_powtwo_ceil (usize n) {
  if (n <= 1) return 1;
  if ((n & (n - 1)) == 0) return n;

  int lz = __builtin_clzll (n);
  int msb_pos = 63 - lz;

  // next power of 2 doesn't fit in usize
  if (msb_pos < 63) return (usize)1 << (msb_pos + 1);

  die ("z3_powtwo_ceil: overflow, no larger power of 2 fits in usize");
}
#endif
