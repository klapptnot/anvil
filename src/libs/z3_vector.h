// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

/// @file z3_vector.h
/// @brief Dynamic array operations with automatic resizing and memory
/// management.
///
/// Provides growable, type-erased dynamic arrays with resizing macros,
/// debug printing utilities, and scoped resource cleanup for array memory.
/// Elements of any size can be stored; retrieval yields a `void*`.
///
/// @note Requires C23 (`-std=c23`) and `z3_toys.h`.
#pragma once

#include <notrust.h>
#include <stddef.h>
#include <z3_toys.h>

/// @brief Initial element capacity allocated on the first push to an empty
/// Vector.
#define Z3_VECTOR_INITIAL_CAPACITY 8

/// @brief Dynamic array structure with automatic resizing
typedef struct {
  usize len;             ///< Current length (how many items it *is* storing)
  usize esz;             ///< Size of each item, in bytes
  usize max;             ///< Total capacity (how many items it *could* store)
  void* val;             ///< Pointer to the raw element storage
  void (*drop) (void*);  ///< required for z3_dropv, ignored by z3_leakv
} Vector;

/// @brief Generic array, frozen (read-only) data
typedef const struct {
  usize len;  ///< Current length
  usize esz;  ///< Size of each item, in bytes
  void* val;  ///< Pointer to the raw element storage
} FrozenVector;

/// @brief Frees its container and all contained values on scope exit
#define OwnedVector [[gnu::cleanup (z3_dropv)]] Vector

/// @brief Frees its container on scope exit, leaving contained values untouched
#define LeakyVector [[gnu::cleanup (z3_leakv)]] Vector

/// @brief Initialize a new, empty dynamic array for a specific type.
/// @param type Element type to be stored in the array.
/// @param dropfn Drop function for each element in array.
/// @return A zero-initialized `Vector` with `esz` set to `sizeof (type)`.
#define z3_vec(type, dropfn)                                  \
  (Vector) {                                                  \
    .max = 0, .len = 0, .esz = sizeof (type), .val = nullptr, \
    .drop = (void (*) (void*)) (void (*) (type*)) {           \
      dropfn                                                  \
    }                                                         \
  }

/// @brief Append an item to the array, growing capacity if needed.
/// @param vec  Pointer to the `Vector` to push onto.
/// @param item Pointer to the item to copy in (`vec->esz` bytes). Pass
/// `&value`, not `value`.
void z3_addv (Vector* vec, const void* item);

/// @brief Free the memory used by a dynamic array and reset it to empty.
/// @param vec A `Vector` to free. Does not free the elements — see
/// z3_dropv ().
void z3_leakv (Vector* vec);

/// @brief Free all elements in a dynamic array using a custom per-element
///        free function (vec->drop field), then free the array itself.
///
/// @param vec     Pointer to the `Vector` to drain.
void z3_dropv (Vector* vec);

/// @brief Remove the element at `idx` by swapping in the last element
/// (order not preserved). Calls `vec->drop` on the removed value first,
/// if set — mirrors z3_dropv semantics; pass a null-drop vector for
/// z3_leakv-style behavior.
/// @param vec Pointer to the `Vector` to remove from.
/// @param idx Element index (0-based) to remove.
void z3_delv (Vector* vec, usize idx);

/// @brief Bulk-copy `count` raw elements into the array, growing capacity
/// if needed. Appends — does not replace. To replace, reset `vec->len = 0`
/// first, then call this.
///
/// @warning Unsafe: does a raw `memcpy` of `count * vec->esz` bytes from
/// @p items with no type checking. Caller must ensure @p items actually
/// holds that many elements of the vector's element type.
///
/// @param vec   Pointer to the `Vector` to load into.
/// @param items Pointer to contiguous source elements.
/// @param count Number of elements to copy in.
void z3_loadv (Vector* vec, const void* items, usize count);

/// @brief Sort in-place using heapsort. O(n log n) time, O(1) extra space.
/// @param vec  Pointer to the `Vector` to sort in-place.
/// @param pred Ordering predicate; if pred (a, b), `a` before `b`.
void z3_sortv (Vector* vec, bool (*pred) (const void*, const void*));

/// @brief Display a dynamic array's contents using a type-specific display
/// function.
///
/// Requires a `z3__display_##type(const type*)` function to exist for @p type.
///
/// @param vec  A `Vector` (by value, not pointer) to print.
/// @param type Element type stored in @p vec; used to select the display
/// function.
#define z3_showv(vec, type)                                            \
  {                                                                    \
    printf (                                                           \
      #vec " = Vector {\n  len: %zu,\n  max: %zu,\n  val: [\n",        \
      (vec).len,                                                       \
      (vec).max                                                        \
    );                                                                 \
    for (usize i = 0; i < (vec).len; i++) {                            \
      z3__display_##type ("    [%zu] = ", i, (type*)z3_getv (vec, i)); \
    }                                                                  \
    printf ("  ]\n}\n");                                               \
  }

/// @brief Print raw debug information (length, capacity, element pointers)
///        about a dynamic array.
/// @param vec A `Vector` (by value, not pointer) to print.
#define z3_dbgv(vec)                                                       \
  {                                                                        \
    printf (                                                               \
      #vec " = Vector {\n  len: %zu,\n  max: %zu,\n", (vec).len, (vec).max \
    );                                                                     \
    for (usize i = 0; i < (vec).len; i++) {                                \
      printf ("  [%zu] = %p,\n", i, z3_getv (vec, i));                     \
    }                                                                      \
    printf ("}\n");                                                        \
  }

/// @brief Push a pointer value, for a Vector of pointers (`z3_vec (T*)`).
/// @param vec Pointer to the target `Vector`.
/// @param ptr The pointer value to push (e.g. from `strdup`/`malloc`).
[[clang::always_inline, maybe_unused]]
static inline void z3_addvp (Vector* vec, void* ptr) {
  z3_addv (vec, (void*)&ptr);
}

/// @brief Get a pointer to the element at `idx`, for a Vector of values (e.g.
/// `String`).
/// @param vec A `Vector` (by value) to index into.
/// @param idx Element index (0-based, unchecked).
/// @return Pointer to the element itself.
[[clang::always_inline, maybe_unused]]
static inline void* z3_getv (Vector vec, usize idx) {
  return (char*)vec.val + (idx * vec.esz);
}

/// @brief Get the pointer value at `idx`, for a Vector of pointers
/// (`z3_vec(T*)`).
/// @param vec The `Vector` to index into.
/// @param idx Element index (0-based, unchecked).
/// @return The stored pointer value.
[[clang::always_inline, maybe_unused]]
static inline void* z3_getvp (Vector vec, usize idx) {
  return *(void**)((char*)vec.val + (idx * vec.esz));
}

#ifdef Z3_VECTOR_IMPL
#include <alloca.h>
#include <stdlib.h>
#include <string.h>

void z3_delv (Vector* vec, usize idx) {
  if (vec->len == 0 || idx >= vec->len)
    die ("z3_delv: index %zu out of bounds (len=%zu)", idx, vec->len);

  void* slot = (char*)vec->val + (idx * vec->esz);
  if (vec->drop) vec->drop (slot);
  if (idx != vec->len - 1) {
    memcpy (slot, (char*)vec->val + ((vec->len - 1) * vec->esz), vec->esz);
  }
  vec->len--;
}

void z3_loadv (Vector* vec, const void* items, usize count) {
  if (vec->len + count > vec->max) {
    usize newmax = (vec->max == 0) ? Z3_VECTOR_INITIAL_CAPACITY : vec->max;
    while (newmax < vec->len + count) newmax *= 2;
    // NOLINTNEXTLINE(bugprone-suspicious-realloc-usage)
    vec->val = realloc (vec->val, vec->esz * newmax);
    if (vec->val == nullptr)
      die ("z3_loadv: requested %zu bytes", newmax * vec->esz);
    vec->max = newmax;
  }
  memcpy ((char*)vec->val + (vec->len * vec->esz), items, count * vec->esz);
  vec->len += count;
}

void z3_addv (Vector* vec, const void* item) {
  if (vec->len >= vec->max) {
    vec->max = (vec->max == 0) ? Z3_VECTOR_INITIAL_CAPACITY : vec->max * 2;
    // NOLINTNEXTLINE(bugprone-suspicious-realloc-usage)
    vec->val = realloc (vec->val, vec->esz * vec->max);
    if (vec->val == nullptr)
      die ("Vector realloc: requested %zu bytes", vec->max * vec->esz);
  }
  memcpy ((char*)vec->val + (vec->len * vec->esz), item, vec->esz);
  vec->len++;
}

#define swap(a, b)                                           \
  {                                                          \
    memcpy (tmp, z3_getv (*vec, a), vec->esz);               \
    memcpy (z3_getv (*vec, a), z3_getv (*vec, b), vec->esz); \
    memcpy (z3_getv (*vec, b), tmp, vec->esz);               \
  }

static inline void z3_sift_down (
  Vector* vec,
  bool (*pred) (const void*, const void*),
  usize root,
  usize heap_len
) {
  void* tmp = alloca (vec->esz);
  while (true) {
    usize largest = root;
    usize left = (2 * root) + 1;
    usize right = (2 * root) + 2;

    if (left < heap_len && pred (z3_getv (*vec, largest), z3_getv (*vec, left)))
      largest = left;
    if (right < heap_len &&
        pred (z3_getv (*vec, largest), z3_getv (*vec, right)))
      largest = right;

    if (largest == root) return;

    swap (root, largest);
    root = largest;
  }
}

void z3_sortv (Vector* vec, bool (*pred) (const void*, const void*)) {
  if (vec->len < 2) return;

  for (usize i = vec->len / 2; i-- > 0;) {
    z3_sift_down (vec, pred, i, vec->len);
  }

  void* tmp = alloca (vec->esz);
  for (usize end = vec->len - 1; end > 0; end--) {
    swap (0, end);
    z3_sift_down (vec, pred, 0, end);
  }
}

#undef swap

void z3_dropv (Vector* vec) {
  if (!vec) return;
  if (!vec->drop)
    die ("z3_dropv: no drop function set; use z3_leakv if intentional");
  for (usize i = 0; i < vec->len; i++) {
    vec->drop ((void*)((char*)vec->val + (i * vec->esz)));
  }
  z3_leakv (vec);
}

void z3_leakv (Vector* vec) {
  if (!vec || !vec->val) return;

  free (vec->val);
  vec->val = nullptr;
  vec->len = 0;
  vec->esz = 0;
  vec->max = 0;
}

#endif
