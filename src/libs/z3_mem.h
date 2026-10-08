#pragma once

#include <notrust.h>
#include <z3_toys.h>
#include <z3_vector.h>

/// @brief Global stack of pending drops
extern Vector Z3_DROPLIST;

/// @brief A single deferred-cleanup entry: a pointer paired with its
/// destructor.
typedef struct {
  void* ptr;             ///< Pointer to the resource to be released.
  void (*drop) (void*);  ///< Destructor to call on `ptr`; `free` used if null.
} DropEntry;

/// @brief Scope marker capturing the drop-list length at scope entry, for
/// rollback on exit.
typedef struct {
  usize offset;  ///< Drop-list length snapshot when this scope began.
} Z3Scope;

/// @brief Cleanup callback for `Z3Scope`; runs all drops registered since scope
/// entry.
/// @param g Scope guard whose captured offset marks where to stop unwinding.
void z3_scope_drop (Z3Scope* g);

/// @brief Runs and clears every pending drop in the global drop list, in LIFO
/// order.
void z3_drop ();

/// @brief Declares a scope-local guard that drops everything registered within
/// it on exit.
#define z3_scope                                       \
  [[gnu::cleanup (z3_scope_drop)]] Z3Scope Z3_CONCAT ( \
    _z3_scope_, __LINE__                               \
  ) = {.offset = Z3_DROPLIST.len}

/// @brief Registers a pointer and its destructor on the global drop list for
/// later cleanup.
/// @param ptr Pointer to the resource to release later.
/// @param drop Destructor to call on @p ptr
#define z3_register(ptr, drop)                                                 \
  z3_register_impl (                                                           \
    Z3_DISCARD_QUAL (ptr), (void (*) (void*)) (void (*) (typeof (ptr))) {drop} \
  )

/// @brief Registers a pointer and its destructor on the global drop list for
/// later cleanup.
/// @param ptr Pointer to the resource to release later.
/// @param drop Destructor to call on @p ptr; pass `nullptr` to use `free`.
[[clang::always_inline, maybe_unused, gnu::nonnull (2)]]
static inline void z3_register_impl (void* ptr, void (*drop) (void*)) {
  z3_addv (&Z3_DROPLIST, &(DropEntry) {.ptr = ptr, .drop = drop});
}

/// @brief `malloc` that registers the result on the current drop list
/// freed automatically when the enclosing `z3_scope` exits.
/// Dies on allocation failure
/// @param size Number of bytes to allocate.
[[clang::always_inline,
  maybe_unused,
  gnu::returns_nonnull,
  gnu::alloc_size (1)]]
static inline void* z3_malloc (usize size) {
  void* ptr = malloc (size);
  if (ptr == nullptr) die ("z3_malloc: requested %zu bytes, got nullptr", size);
  z3_register_impl (ptr, free);
  return ptr;
}

/// @brief `calloc` that registers the result on the current drop list
/// freed automatically when the enclosing `z3_scope` exits.
/// Dies on allocation failure
/// @param count Number of elements.
/// @param size Size of each element in bytes.
[[clang::always_inline,
  maybe_unused,
  gnu::returns_nonnull,
  gnu::alloc_size (1, 2)]]
static inline void* z3_calloc (usize count, usize size) {
  void* ptr = calloc (count, size);
  if (ptr == nullptr)
    die ("z3_calloc: requested %zu * %zu bytes, got nullptr", count, size);
  z3_register_impl (ptr, free);
  return ptr;
}

#ifdef Z3_MEM_IMPL
#include <stdlib.h>

/// @brief Global stack of pending drops; defined under `Z3_MEM_IMPL`.
Vector Z3_DROPLIST = z3_vec (DropEntry, nullptr);

void z3_scope_drop (Z3Scope* g) {
  while (Z3_DROPLIST.len > g->offset) {
    DropEntry* e = z3_getv (Z3_DROPLIST, --Z3_DROPLIST.len);
    e->drop (e->ptr);
  }
}

void z3_drop () {
  for (usize i = Z3_DROPLIST.len; i-- > 0;) {
    DropEntry* e = z3_getv (Z3_DROPLIST, i);
    e->drop (e->ptr);
  }
  Z3_DROPLIST.len = 0;
  z3_leakv (&Z3_DROPLIST);
}
#endif
