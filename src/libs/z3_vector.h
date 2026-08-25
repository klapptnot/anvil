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

#include <notrust.h>
#include <stddef.h>
#include <z3_toys.h>

/// @brief Initial element capacity allocated on the first push to an empty Vector.
#define Z3_VECTOR_INITIAL_CAPACITY 8

/// @brief Dynamic array structure with automatic resizing
typedef struct {
  usize max;  ///< Total capacity (how many items it *could* store)
  usize len;  ///< Current length (how many items it *is* storing)
  usize esz;  ///< Size of each item, in bytes
  void* val;  ///< Pointer to the raw element storage
} Vector;

/// @brief Generic array, frozen (read-only) data
typedef const struct {
  usize len;  ///< Current length
  usize esz;  ///< Size of each item, in bytes
  void* val;  ///< Pointer to the raw element storage
} FrozenVector;

/// @brief Declare a typed, named alias for a simple `{ len; T* name; }` array view.
///
/// Expands to an anonymous struct with a `usize len` field and a `T*`
/// field named @p name. Distinct from `Vector` — this has no capacity
/// tracking or resize support of its own.
///
/// @param T    Element type of the array.
/// @param name Name to give the pointer member.
#define Vector(T, name) \
  struct {              \
    usize len;          \
    T* name;            \
  }

/// @brief Initialize a new, empty dynamic array for a specific type.
/// @param type Element type to be stored in the array.
/// @return A zero-initialized `Vector` with `esz` set to `sizeof(type)`.
#define z3_vec(type)                                         \
  (Vector) {                                                 \
    .max = 0, .len = 0, .esz = sizeof (type), .val = nullptr \
  }

/// @brief Append an item to the array, growing capacity if needed.
/// @param vec  Pointer to the `Vector` to push onto.
/// @param item Pointer to the item to copy in (`vec->esz` bytes). Pass `&value`, not `value`.
void z3_push (Vector* vec, const void* item);

/// @brief Get a pointer to the element at `idx`, for a Vector of values (e.g. `String`).
/// @param vec A `Vector` (by value) to index into.
/// @param idx Element index (0-based, unchecked).
/// @return Pointer to the element itself.
void* z3_get (Vector vec, usize idx);

/// @brief Push a pointer value, for a Vector of pointers (`z3_vec(T*)`).
/// @param vec Pointer to the target `Vector`.
/// @param ptr The pointer value to push (e.g. from `strdup`/`malloc`).
void z3_push_ptr (Vector* vec, void* ptr);

/// @brief Get the pointer value at `idx`, for a Vector of pointers (`z3_vec(T*)`).
/// @param vec The `Vector` to index into.
/// @param idx Element index (0-based, unchecked).
/// @return The stored pointer value.
void* z3_get_ptr (Vector vec, usize idx);

/// @brief Display a dynamic array's contents using a type-specific display function.
///
/// Requires a `z3__display_##type(const type*)` function to exist for @p type.
///
/// @param vec  A `Vector` (by value, not pointer) to print.
/// @param type Element type stored in @p vec; used to select the display function.
#define z3_vec_show(vec, type)                                                               \
  {                                                                                          \
    printf (#vec " = Vector {\n  len: %zu,\n  max: %zu,\n  val: [\n", (vec).len, (vec).max); \
    for (usize i = 0; i < (vec).len; i++) {                                                  \
      printf ("    [%zu] = ", i);                                                            \
      z3__display_##type ((type*)z3_get (vec, i));                                           \
      putchar ('\n');                                                                        \
    }                                                                                        \
    printf ("  ]\n}\n");                                                                     \
  }

/// @brief Print raw debug information (length, capacity, element pointers)
///        about a dynamic array.
/// @param vec A `Vector` (by value, not pointer) to print.
#define z3_vec_dbg(vec)                                                            \
  {                                                                                \
    printf (#vec " = Vector {\n  len: %zu,\n  max: %zu,\n", (vec).len, (vec).max); \
    for (usize i = 0; i < (vec).len; i++) {                                        \
      printf ("  [%zu] = %p,\n", i, z3_get (vec, i));                              \
    }                                                                              \
    printf ("}\n");                                                                \
  }

/// @brief Free the memory used by a dynamic array and reset it to empty.
/// @param vec A `Vector` to free. Does not free the elements — see z3_vec_drain().
void z3_vec_drop (Vector* vec);

/// @brief Free all elements in a dynamic array using a custom per-element
///        free function, then free the array itself.
///
/// @p drop_fn is responsible for freeing an element's memory. Called
/// `drop_fn (ptr)`, where `ptr` points directly to element.
/// For a `Vector` of raw pointers, `&free` as drop function.
///
/// @param vec     Pointer to the `Vector` to drain.
/// @param drop_fn Drop for element in Vector.
void z3_vec_drain (Vector* vec, void (*drop_fn) (void*));

/// @brief Define a cleanup function for a specific Vector element type,
///        freeing each element with @p FUNC before freeing the array.
///
/// Expands to a `static inline void z3_vec_drop_##TYPE(Vector*)` function,
/// suitable for use with `__attribute__((cleanup(...)))` via ScopedVector_().
///
/// @param TYPE Element type stored in the Vector.
/// @param FUNC Per-element cleanup function, called as `FUNC((TYPE*)element_ptr)`.
#define z3_vec_drop_for(TYPE, FUNC)                                                       \
  static inline void z3_vec_drop_##TYPE (Vector* vec) {                                   \
    for (usize i = 0; i < vec->len; i++) {                                                \
      /* NOLINTNEXTLINE(cast-align) */                                                    \
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
  static void z3_vec_drop_##TYPE (Vector* vec)

/// @brief Declare a Vector with automatic cleanup via a type-specific drop
///        function defined with z3_vec_drop_fn().
/// @param TYPE Element type stored in the Vector; selects `z3_vec_drop_##TYPE`
///             as the cleanup function.
#define ScopedVector_(TYPE) __attribute__ ((cleanup (z3_vec_drop_##TYPE))) Vector

/// @brief Declare a Vector with automatic generic cleanup (z3_vec_drop()).
///        Does not free individual elements, only the backing storage.
#define ScopedVector __attribute__ ((cleanup (z3_vec_drop))) Vector

#ifdef Z3_VECTOR_IMPL
#include <stdlib.h>
#include <string.h>

void z3_push (Vector* vec, const void* item) {
  if (vec->len >= vec->max) {
    vec->max = (vec->max == 0) ? Z3_VECTOR_INITIAL_CAPACITY : vec->max * 2;
    // NOLINTNEXTLINE(bugprone-suspicious-realloc-usage)
    vec->val = realloc (vec->val, vec->esz * vec->max);
    if (vec->val == nullptr) die ("Vector realloc: requested %zu bytes", vec->max * vec->esz);
  }
  memcpy ((char*)vec->val + (vec->len * vec->esz), item, vec->esz);
  vec->len++;
}

inline void z3_push_ptr (Vector* vec, void* ptr) {
  z3_push (vec, (void*)&ptr);
}

inline void* z3_get (Vector vec, usize idx) {
  return (char*)vec.val + (idx * vec.esz);
}

inline void* z3_get_ptr (Vector vec, usize idx) {
  return *(void**)((char*)vec.val + (idx * vec.esz));
}

void z3_vec_drain (Vector* vec, void (*drop_fn) (void*)) {
  for (usize i = 0; i < vec->len; i++) {
    drop_fn ((void*)((char*)vec->val + (i * vec->esz)));
  }
  z3_vec_drop (vec);
}

inline void z3_vec_drop (Vector* vec) {
  if (!vec || !vec->val) return;

  free (vec->val);
  vec->val = nullptr;
  vec->len = 0;
  vec->esz = 0;
  vec->max = 0;
}

#endif  // Z3_VECTOR_IMPL
