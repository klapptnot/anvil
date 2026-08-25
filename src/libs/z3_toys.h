// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

/**
 * z3_toys.h
 *
 * Description:
 *   General utility macros and functions for common operations.
 *
 * Features:
 *   - Error printing macros
 *   - Warning silence utilities
 *   - Array manipulation helpers
 *
 * Requires:
 *   - C23 Standard (Use -std=c23).
 *
 */
#pragma once

#ifndef __STDC_VERSION__
#error A modern C standard (like C23) is required
#elif __STDC_VERSION__ < 202311L
#error This code must be compiled with -std=c23
#endif

#include <notrust.h>
#include <stdlib.h>
#include <unistd.h>
#if defined(__GNUC__) || defined(__clang__)
/// @brief Suppress `-Wcast-qual` warnings around a declaration/statement.
///
/// We enable **all** warnings, so this exists to opt out of just this one,
/// locally, without silencing it project-wide.
///
/// @param decl The declaration or statement to wrap.
#define KILL_CAST_QUAL(decl)                           \
  _Pragma ("GCC diagnostic push")                      \
    _Pragma ("GCC diagnostic ignored \"-Wcast-qual\"") \
      decl _Pragma ("GCC diagnostic pop")
#else
/// @brief No-op fallback for non-GCC/Clang compilers; unused-variable
/// suppression isn't available so `decl` passes through unchanged.
/// @param decl The declaration or statement to wrap.
#define IGNORE_UNUSED(decl)  decl

/// @brief No-op fallback for non-GCC/Clang compilers; `-Wcast-qual`
/// suppression isn't available so `decl` passes through unchanged.
/// @param decl The declaration or statement to wrap.
#define KILL_CAST_QUAL(decl) decl
#endif

/// @brief Print a formatted error message to stderr in red.
/// @param fmt printf-style format string.
/// @param ... Arguments matching `fmt`, if any.
#define errpfmt(fmt, ...)                                                      \
  (void)fprintf (                                                              \
    stderr, "\x1b[38;5;9m[ERROR] " fmt "\x1b[0m\n" __VA_OPT__ (, ) __VA_ARGS__ \
  )

/// @brief Print a formatted message to stderr, without color or an [ERROR] tag.
///
/// Just saves having to write `stderr` and the NOLINT for the return value
/// every time.
///
/// @param fmt printf-style format string.
/// @param ... Arguments matching `fmt`, if any.
#define eprintf(fmt, ...) \
  (void)fprintf (stderr, fmt __VA_OPT__ (, ) __VA_ARGS__)

/// @brief Print a formatted error message and terminate the process
/// immediately.
///
/// Flushes stderr and calls `_exit(1)`, so no atexit handlers or stdio
/// buffers other than stderr are flushed.
///
/// @param fmt printf-style format string.
/// @param ... Arguments matching `fmt`, if any.
#define die(fmt, ...)           \
  {                             \
    errpfmt (fmt, __VA_ARGS__); \
    (void)fflush (stderr);      \
    _exit (1);                  \
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
#define popf(c, v) /* NOLINT(concurrency-mt-unsafe) */           \
  (c > 0 ? (--c, *v++)                                           \
         : (errpfmt ("Trying to access a non-existent value\n"), \
             exit (EXIT_FAILURE),                                \
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

/// @brief Calculate the next power of 2 greater than or equal to n
/// @param n Lower bound value
/// @return The smallest power of 2 that is `>= n`
usize powtwo_ceil (usize n);

#ifdef Z3_TOYS_IMPL
// Implementation of utility functions

//~ Calculate the next power of 2 greater than or equal to n
usize powtwo_ceil (usize n) {
  if (n <= 1) return 1;
  if ((n & (n - 1)) == 0) return n;

  int lz = __builtin_clzll (n);
  int msb_pos = 63 - lz;

  // next power of 2 doesn't fit in usize
  if (msb_pos < 63) return (usize)1 << (msb_pos + 1);

  die ("powtwo_ceil: overflow, no larger power of 2 fits in usize");
}

#endif  // Z3_TOYS_IMPL
