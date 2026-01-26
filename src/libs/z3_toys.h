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

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define IGNORE_UNUSED(declaration)                                                \
  _Pragma ("GCC diagnostic push") _Pragma ("GCC diagnostic ignored \"-Wunused\"") \
    _Pragma ("GCC diagnostic ignored \"-Wunused-result\"")                        \
      declaration _Pragma ("GCC diagnostic pop")

// I enable **all** warnings, I want to pick which one to silence
// but still get them
#define IGNORE_WARNING(declaration)                                            \
  _Pragma ("GCC diagnostic push") _Pragma ("GCC diagnostic ignored \"-Wall\"") \
    declaration _Pragma ("GCC diagnostic pop")
#else
#define IGNORE_UNUSED(declaration)  declaration
#define IGNORE_WARNING(declaration) declaration
#endif

//~ Print formatted error message to stderr with red coloring
#define errpfmt(fmt, ...)                                                    \
  /* NOLINT (cert-err33-c) */ fprintf (                                      \
    stderr, "\x1b[38;5;9m[ERROR] " fmt "\x1b[0m" __VA_OPT__ (, ) __VA_ARGS__ \
  )

//~ simply avoid adding stderr and nolint to use return value
#define eprintf(fmt, ...) \
  /* NOLINT (cert-err33-c) */ fprintf (stderr, fmt __VA_OPT__ (, ) __VA_ARGS__)

#define die(fmt, ...)                            \
  {                                              \
    errpfmt (fmt, __VA_ARGS__);                  \
    /* NOLINT (cert-err33-c) */ fflush (stderr); \
    _exit (1);                                   \
  }

#define popf(c, v) /* NOLINT(concurrency-mt-unsafe) */           \
  (c > 0 ? (--c, *v++)                                           \
         : (errpfmt ("Trying to access a non-existent value\n"), \
            exit (EXIT_FAILURE),                                 \
            (typeof (*v))0))

#define CHECK_OR_RETURN(expr, ret_val) \
  if ((expr) < 0) return (ret_val)

#define CHECK_OR_EXIT(expr, msg) \
  if ((expr) < 0) {              \
    perror (msg);                \
    exit (EXIT_FAILURE);         \
  }

//~ Calculate the next power of 2 greater than or equal to n
size_t next_power_of2 (size_t n);

#ifdef Z3_TOYS_IMPL
// Implementation of utility functions

//~ Calculate the next power of 2 greater than or equal to n
size_t next_power_of2 (size_t n) {
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  return n + 1;
}

#endif  // Z3_TOYS_IMPL
