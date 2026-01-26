// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

/**
 * z3_vector.h
 *
 * Description:
 *   Provides dynamic array operations with automatic resizing and memory management.
 *
 * Features:
 *   - Dynamic arrays with automatic resizing
 *   - Type-safe array manipulation macros
 *   - Debug printing utilities
 *   - Scoped resource cleanup for dynamic arrays
 *
 * Requires:
 *   - C23 Standard (Use -std=c23).
 *   - z3_toys.h
 */
#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <z3_toys.h>

#define Z3_VECTOR_INITIAL_CAPACITY 16

//~ Dynamic array structure with automatic resizing
typedef struct {
  size_t max;  // total capacity (how many items I *could* store)
  size_t len;  // current length (how many items I *am* storing)
  size_t esz;  // size of each item
  void* val;   // void pointer to the actual data
} Vector;

//~ Initialize a new dynamic array for a specific type
#define z3_vec(type)                                         \
  (Vector) {                                                 \
    .max = 0, .len = 0, .esz = sizeof (type), .val = nullptr \
  }

//~ Initialize with a specific size, otherwise push will do it
#define z3_vec_init_capacity(vec, cap)                                                  \
  {                                                                                     \
    /* NOLINTNEXTLINE(bugprone-suspicious-realloc-usage) */                             \
    (vec).val = malloc ((vec).esz * (vec).max);                                         \
    if ((vec).val == nullptr) die ("Vector realloc: requested %zu bytes\n", (vec).max); \
  }

//~ Get a pointer to an element at a specific index
#define z3_get(vec, idx) ((typeof ((vec).val))((char*)(vec).val + ((idx) * (vec).esz)))

//~ Display the dynamic array
#define z3_vec_show(vec, type)                                                               \
  {                                                                                          \
    printf (#vec " = Vector {\n  len: %zu,\n  max: %zu,\n  val: [\n", (vec).len, (vec).max); \
    for (size_t i = 0; i < (vec).len; i++) {                                                 \
      printf ("    %-4zu -> ", i);                                                           \
      z3__display_##type ((type*)z3_get (vec, i));                                           \
      putchar ('\n');                                                                        \
    }                                                                                        \
    printf ("  ]\n}\n");                                                                     \
  }

//~ Print debug information about a dynamic array
#define z3_vec_dbg(vec)                                                            \
  {                                                                                \
    printf (#vec " = Vector {\n  len: %zu,\n  max: %zu,\n", (vec).len, (vec).max); \
    for (size_t i = 0; i < (vec).len; i++) {                                       \
      printf ("  [%zu] = %p,\n", i, z3_get (vec, i));                              \
    }                                                                              \
    printf ("}\n");                                                                \
  }

//~ Append an item to a dynamic array, resizing if necessary
#define z3_push(vec, item)                                                                \
  {                                                                                       \
    if ((vec).len >= (vec).max) {                                                         \
      (vec).max = (((vec).max == 0) ? Z3_VECTOR_INITIAL_CAPACITY : (vec).max * 2);        \
      /* NOLINTNEXTLINE(bugprone-suspicious-realloc-usage) */                             \
      (vec).val = realloc ((vec).val, (vec).esz * (vec).max);                             \
      if ((vec).val == nullptr) die ("Vector realloc: requested %zu bytes\n", (vec).max); \
    }                                                                                     \
    memcpy (z3_get (vec, (vec).len), (void*)&(item), (vec).esz);                          \
    (vec).len++;                                                                          \
  }

//~ Free the memory used by a dynamic array
#define z3_drop_vec(vec) \
  if ((vec).val) {       \
    free ((vec).val);    \
    (vec).val = nullptr; \
    (vec).esz = 0;       \
    (vec).max = 0;       \
    (vec).len = 0;       \
  }

//~ Free all elements in a dynamic array using a custom free function
#define z3_drain(vec, type, drop_fn)         \
  {                                          \
    for (size_t i = 0; i < (vec).len; i++) { \
      drop_fn (((type*)(vec).val)[i]);       \
    }                                        \
    z3_drop_vec (vec);                       \
  }

//~ Cleanup function for generic Vector (used with attribute cleanup)
void z3_vec_drop (Vector* vec);

//~ Define a cleanup function for a specific Vector type
#define z3_vec_drop_fn(TYPE, FUNC)                                                        \
  static inline void z3_vec_drop_##TYPE (Vector* vec) {                                   \
    for (size_t i = 0; i < vec->len; i++) {                                               \
      /* NOLINTNEXTLINE (cast-align) */                                                   \
      _Pragma ("GCC diagnostic push") _Pragma ("GCC diagnostic ignored \"-Wcast-align\"") \
        FUNC ((TYPE*)((char*)vec->val + ((i) * vec->esz)));                               \
      _Pragma ("GCC diagnostic pop")                                                      \
    }                                                                                     \
    free (vec->val);                                                                      \
    vec->val = nullptr;                                                                   \
    vec->len = 0;                                                                         \
    vec->esz = 0;                                                                         \
    vec->max = 0;                                                                         \
  }                                                                                       \
  static inline void z3_vec_drop_##TYPE (Vector* vec)

//~ Define a Vector with automatic cleanup for a specific type
#define ScopedVector_(TYPE) __attribute__ ((cleanup (z3_vec_drop_##TYPE))) Vector

//~ Define a Vector with automatic generic cleanup
#define ScopedVector __attribute__ ((cleanup (z3_vec_drop))) Vector

#ifdef Z3_VECTOR_IMPL

//~ Cleanup function for generic Vector (used with attribute cleanup)
inline void z3_vec_drop (Vector* vec) {
  if (!vec || !vec->val) return;

  free (vec->val);
  vec->val = nullptr;
  vec->len = 0;
  vec->esz = 0;
  vec->max = 0;
}

#endif  // Z3_VECTOR_IMPL
