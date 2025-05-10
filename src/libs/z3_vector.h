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
 *
 */
#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "z3_toys.h"

#define Z3_VECTOR_INITIAL_CAPACITY 32

//~ Dynamic array structure with automatic resizing
typedef struct {
  size_t max;  // total capacity (how many items I *could* store)
  size_t len;  // current length (how many items I *am* storing)
  size_t esz;  // size of each item
  void *val;   // void pointer to the actual data
} Z3Vector;

//~ Initialize a new dynamic array for a specific type
#define z3_vec(type)                                      \
  (Z3Vector) {                                            \
    .max = 0, .len = 0, .esz = sizeof (type), .val = NULL \
  }

// Define a heap-allocated vector function
extern Z3Vector *z3_vec_heap (size_t element_size);

// Helper macro to make the element size specification cleaner
#define z3_new_vec(type) z3_vec_heap (sizeof (type))

//~ Get a pointer to an element at a specific index
#define z3_get(vec, idx) ((typeof ((vec).val))((uintptr_t)(vec).val + (idx) * (vec).esz))

//~ Display the dynamic array
#define z3_vec_show(vec, type)                                                       \
  {                                                                                  \
    printf (#vec " = Z3Vector {\n  len: %zu,\n  max: %zu,\n", (vec).len, (vec).max); \
    for (size_t i = 0; i < (vec).len; i++) {                                         \
      printf ("  [%zu] = ", i);                                                      \
      __dynarray_print_##type ((type *)z3_get (vec, i));                             \
      putchar ('\n');                                                                \
    }                                                                                \
    printf ("}\n");                                                                  \
  }

//~ Print debug information about a dynamic array
#define z3_vec_dbg(vec)                                                              \
  {                                                                                  \
    printf (#vec " = Z3Vector {\n  len: %zu,\n  max: %zu,\n", (vec).len, (vec).max); \
    for (size_t i = 0; i < (vec).len; i++) {                                         \
      printf ("  [%zu] = %p,\n", i, z3_get (vec, i));                                \
    }                                                                                \
    printf ("}\n");                                                                  \
  }

//~ Append an item to a dynamic array, resizing if necessary
#define z3_push(vec, item)                                                         \
  {                                                                                \
    if ((vec).len >= (vec).max) {                                                  \
      (vec).max = (((vec).max == 0) ? Z3_VECTOR_INITIAL_CAPACITY : (vec).max * 2); \
      (vec).val = realloc ((vec).val, (vec).esz * (vec).max);                      \
      if ((vec).val == NULL) {                                                     \
        eprintf ("Z3Vector realloc: requested %zu bytes\n", (vec).max);            \
        exit (EXIT_FAILURE);                                                       \
      }                                                                            \
    }                                                                              \
    memcpy (z3_get (vec, (vec).len), &(item), (vec).esz);                          \
    (vec).len++;                                                                   \
  }

//~ Free the memory used by a dynamic array
#define z3_drop_vec(vec) \
  if ((vec).val) {       \
    free ((vec).val);    \
    (vec).val = NULL;    \
    (vec).esz = 0;       \
    (vec).max = 0;       \
    (vec).len = 0;       \
  }

//~ Free the memory used by a dynamic array
#define z3_drop_heap_vec(vec) \
  if ((vec).val) {            \
    free ((vec).val);         \
  }                           \
  free ((vec));

//~ Free all elements in a dynamic array using a custom free function
#define z3_drain(vec, type, drop_fn)         \
  {                                          \
    for (size_t i = 0; i < (vec).len; i++) { \
      drop_fn (((type *)(vec).val)[i]);      \
    }                                        \
    z3_drop_vec (vec);                       \
  }

#ifdef Z3_TOYS_SCOPED
//~ Cleanup function for generic Z3Vector (used with attribute cleanup)
void __cleanup_Z3Vector_generic_pod (Z3Vector *d);

//~ Define a cleanup function for a specific Z3Vector type
#define z3_dropfn(TYPE, FUNC)                                  \
  static inline void __cleanup_Z3Vector_##TYPE (Z3Vector *d) { \
    for (size_t i = 0; i < d->len; i++) {                      \
      FUNC ((TYPE *)((uintptr_t)d->val + (i) * d->esz));       \
    }                                                          \
    free (d->val);                                             \
    d->val = NULL;                                             \
    d->len = 0;                                                \
    d->esz = 0;                                                \
    d->max = 0;                                                \
  }                                                            \
  static inline void __cleanup_Z3Vector_##TYPE (Z3Vector *d)

//~ Define a Z3Vector with automatic cleanup for a specific type
#define ScopedZ3Vector_(TYPE) __attribute__ ((cleanup (__cleanup_Z3Vector_##TYPE))) Z3Vector

//~ Define a Z3Vector with automatic generic cleanup
#define ScopedZ3Vector __attribute__ ((cleanup (__cleanup_Z3Vector_generic_pod))) Z3Vector
#endif  // Z3_TOYS_SCOPED

#ifdef Z3_VECTOR_IMPL

#ifdef Z3_TOYS_SCOPED
//~ Cleanup function for generic Z3Vector (used with attribute cleanup)
void __cleanup_Z3Vector_generic_pod (Z3Vector *d) {
  if (!d || !d->val) return;

  free (d->val);
  d->val = NULL;
  d->len = 0;
  d->esz = 0;
  d->max = 0;
}

// Define a heap-allocated vector function (not a macro)
inline Z3Vector *z3_vec_heap (size_t element_size) {
  Z3Vector *vec = (Z3Vector *)malloc (sizeof (Z3Vector));
  if (vec) {
    vec->max = 0;
    vec->len = 0;
    vec->esz = element_size;
    vec->val = NULL;
  }
  return vec;
}

#endif  // Z3_TOYS_SCOPED

#endif  // Z3_VECTOR_IMPL
