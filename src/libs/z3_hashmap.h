// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

/// @file z3_hashmap.h
/// @brief Memory-efficient hashmap implementation, HashMap<cnstr, any*>.
///
/// Provides string-key to value mapping using FNV-1a hashing with
/// linear probing for collisions, bit-packed occupation tracking, and
/// automatic memory management for keys and values.
///
/// @note Requires C23 (`-std=c23`) and `z3_toys.h`.
#pragma once

#include <notrust.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <z3_toys.h>

#define Z3_HM_BF_SIZE  sizeof (usize)
#define Z3_HM_BF_CAP   (sizeof (usize) * 8)
#define Z3_HM_INIT_CAP (Z3_HM_BF_CAP < 32 ? Z3_HM_BF_CAP : 32)

/// @brief A single slot in a HashMap's entry table.
typedef struct {
  u64 hash;   ///< Cached hash of `key`, used to speed up probing/comparison
  nstr key;   ///< Owned, heap-duplicated copy of the key
  void* val;  ///< Pointer to the associated value. Not owned by the map.
} HashMapEntry;

/// @brief Auto-growing HashMap<String, &T>
typedef struct {
  HashMapEntry* beds;    ///< Backing array of entries (open-addressed table)
  usize max;             ///< Current capacity of `beds`
  usize len;             ///< Number of entries currently stored
  usize* bfs;            ///< Bitset/flags tracking occupied/tombstoned slots
  void (*drop) (void*);  ///< required for z3_dropm, ignored by z3_leakm
} HashMap;

/// @brief Iterator state for walking a HashMap's entries
typedef struct {
  HashMap* map;  ///< Pointer to the map being iterated
  usize idx;     ///< Current index in the table
  cnstr key;     ///< Key of the current entry, set after z3_nextm ()
  void* val;     ///< Value of the current entry, set after z3_nextm ()
} HashMapIterator;

/// @brief Frees its container and all contained values on scope exit
#define OwnedHashMap [[gnu::cleanup (z3_dropm)]] HashMap

/// @brief Frees its container on scope exit, leaving contained values untouched
#define LeakyHashMap [[gnu::cleanup (z3_leakm)]] HashMap

/// @brief Create a new empty hashmap
/// @param dropfn Drop function for each element in array.
/// @return A zero-initialized `HashMap` ready for use.
#define z3_map(dropfn)                                   \
  (HashMap) {                                            \
    .beds = nullptr, .max = 0, .len = 0, .bfs = nullptr, \
    .drop = (void (*) (void*)) (dropfn)                  \
  }

/// @brief Create a new empty hashmap with default capacity
/// @return A new initialized `HashMap`
HashMap z3_newm ();

/// @brief Insert or update a key-value pair in the hashmap
///
/// The key is duplicated internally, do not inline strdup. Updating a
/// value does NOT replace the key; again, no strdup.
///
/// @param map Pointer to the target `HashMap`
/// @param key The key to insert or update
/// @param value Pointer to the value to associate with `key`
void z3_addm (HashMap* map, cnstr key, void* value);

/// @brief Retrieve a value by its key
/// @param map Pointer to the `HashMap`
/// @param key The key to look up
/// @return The associated value, or `NULL` if the key doesn't exist
void* z3_getm (HashMap* map, cnstr key);

/// @brief Remove a key-value pair from the hashmap
/// @param map Pointer to the `HashMap`
/// @param key The key to remove
void z3_delm (HashMap* map, cnstr key);

/// @brief Free all memory associated with the hashmap, including the values it
/// owns
/// @param map Pointer to the `HashMap` to clean up
void z3_dropm (HashMap* map);

/// @brief Free the hashmap and the keys it owns, leaving values orphaned
///
/// Unlike z3_dropm (), this only releases the map's internal
/// structure and its owned keys — the values are not touched and must
/// be freed by the caller if needed.
///
/// @param map Pointer to the `HashMap` to clean up
void z3_leakm (HashMap* map);

/// @brief Construct a zero-initialized `HashMapIterator` for the given map.
/// @param map Pointer to the `HashMap` to iterate.
/// @return A `HashMapIterator` literal ready to pass to z3_nextm ().
HashMapIterator z3_iterm (HashMap* map);

/// @brief Advances the iterator to the next valid entry in the map
/// @param it Pointer to the `HashMapIterator` to advance
/// @return `true` if an entry was found, `false` if iteration is complete
bool z3_nextm (HashMapIterator* it);

/// @brief Check if a key exists in the hashmap
/// @param map Pointer to the `HashMap`
/// @param key The key to check
/// @return `true` if the key exists, `false` otherwise
[[clang::always_inline, maybe_unused]]
static inline bool z3_hasm (HashMap* map, cnstr key) {
  return z3_getm (map, key) != nullptr;
}

#ifdef Z3_HASHMAP_IMPL
#include <stdlib.h>
#include <string.h>

static u64 z3__hm_hash_str (cnstr str) {
  // FNV-1a hash
  u64 hash = 14695981039346656037ULL;  // NOLINT(readability-magic-numbers)
  while (*str) {
    hash ^= (u8)(*str++);
    hash *= 1099511628211ULL;  // NOLINT(readability-magic-numbers)
  }
  return hash;
}

static usize z3__hm_probe (u64 hash, usize i, usize cap) {
  return (hash + i) % cap;
}

static bool z3__hm_pos_is_used (const usize* bf, usize pos) {
  usize home = pos / Z3_HM_BF_CAP;  // Which u64
  u8 room = pos % Z3_HM_BF_CAP;     // Which bit in that u64

  return (bool)((bf[home] >> room) & 1);
}

static void z3__hm_set_used (usize* bf, usize pos) {
  usize home = pos / Z3_HM_BF_CAP;
  u8 room = pos % Z3_HM_BF_CAP;

  bf[home] |= (1ULL << room);
}

HashMap z3_newm () {
  HashMap map = {0};
  usize alloc_size = (Z3_HM_INIT_CAP * sizeof (HashMapEntry)) + Z3_HM_BF_SIZE;

  map.max = Z3_HM_INIT_CAP;
  map.len = 0;
  map.bfs = malloc (alloc_size);
  if (map.bfs == nullptr)
    die ("z3_map: requested %zu bytes, got nullptr", alloc_size);
  map.beds = (HashMapEntry*)(map.bfs + 1);
  return map;
}

void z3_addm (HashMap* map, cnstr key, void* value) {
  if (!key || !value) return;

  if (map->len >= map->max * 3 / 4) {
    usize old_capacity = map->max;
    HashMapEntry* old_beds = map->beds;
    usize* exes = map->bfs;

    map->len = 0;
    map->max = map->max == 0 ? Z3_HM_INIT_CAP : map->max * 2;

    usize new_cap = /* round up */
      (map->max + (Z3_HM_BF_CAP - 1)) / Z3_HM_BF_CAP;
    usize alloc_size =
      (map->max * sizeof (HashMapEntry)) + (new_cap * Z3_HM_BF_SIZE);

    map->bfs = malloc (alloc_size);
    memset (map->bfs, 0, alloc_size);
    map->beds = (HashMapEntry*)(map->bfs + new_cap);

    if (map->bfs == nullptr)
      die ("z3_addm: requested %zu bytes, got nullptr", alloc_size);

    // rehash all existing entries
    for (usize i = 0; i < old_capacity; ++i) {
      if (old_beds[i].key != nullptr) {
        u64 hash = old_beds[i].hash;
        for (usize j = 0; j < map->max; ++j) {
          usize idx = z3__hm_probe (hash, j, map->max);
          if (!z3__hm_pos_is_used (map->bfs, idx)) {
            HashMapEntry* entry = &map->beds[idx];

            entry->key = old_beds[i].key;
            entry->val = old_beds[i].val;
            entry->hash = hash;
            map->len++;

            z3__hm_set_used (map->bfs, idx);
            break;
          }
        }
      }
    }
    free (exes);
  }

  u64 hash = z3__hm_hash_str (key);
  for (usize i = 0; i < map->max; ++i) {
    usize idx = z3__hm_probe (hash, i, map->max);
    HashMapEntry* entry = &map->beds[idx];
    bool is_used = z3__hm_pos_is_used (map->bfs, idx);
    if (!is_used || (entry->key && strcmp (entry->key, key) == 0)) {
      z3__hm_set_used (map->bfs, idx);
      if (!is_used) map->len++;

      // key is the same if present
      if (!entry->key) entry->key = strdup (key);
      if (entry->val) free (entry->val);

      entry->val = value;
      entry->hash = hash;
      return;
    }
  }
}

void* z3_getm (HashMap* map, cnstr key) {
  if (!key) return nullptr;
  u64 hash = z3__hm_hash_str (key);
  for (usize i = 0; i < map->max; ++i) {
    usize idx = z3__hm_probe (hash, i, map->max);

    HashMapEntry* entry = &map->beds[idx];
    bool is_used = z3__hm_pos_is_used (map->bfs, idx);
    if ((i32)is_used && (entry->key && strcmp (entry->key, key) == 0)) {
      return entry->val;
    }

    if (!is_used) break;
  }
  return nullptr;
}

void z3_delm (HashMap* map, cnstr key) {
  if (!key) return;
  u64 hash = z3__hm_hash_str (key);
  for (usize i = 0; i < map->max; ++i) {
    usize idx = z3__hm_probe (hash, i, map->max);
    HashMapEntry* entry = &map->beds[idx];
    bool is_used = z3__hm_pos_is_used (map->bfs, idx);
    if ((i32)is_used && (entry->key && strcmp (entry->key, key) == 0)) {
      // never setting pos_used entry to false
      // if (is_used && key == nullptr) /* was deleted, free tombstone */
      // very few changes needed

      free ((void*)entry->key);
      free (entry->val);
      entry->key = nullptr;
      entry->val = nullptr;

      map->len--;
      return;
    }
  }
}

HashMapIterator z3_iterm (HashMap* map) {
  if (!map) die ("z3_iterm: invalid map, nullptr");
  return (HashMapIterator) {
    .map = map, .idx = 0, .key = nullptr, .val = nullptr
  };
}

bool z3_nextm (HashMapIterator* it) {
  if (it->map->len == 0) return false;
  while (it->idx < it->map->max) {
    usize i = it->idx++;

    if (z3__hm_pos_is_used (it->map->bfs, i) && it->map->beds[i].key) {
      it->key = it->map->beds[i].key;
      it->val = it->map->beds[i].val;
      return true;
    }
  }
  return false;
}

void z3_dropm (HashMap* map) {
  if (!map) return;
  if (!map->drop)
    die ("z3_dropm: no drop function set; use z3_leakm if intentional");
  HashMapIterator it = z3_iterm (map);
  while (z3_nextm (&it)) {
    Z3_DISCARD_QUAL (free (it.key));
    map->drop (it.val);
  }
  free (map->bfs);
}

void z3_leakm (HashMap* map) {
  if (!map || !map->bfs) return;
  HashMapIterator it = z3_iterm (map);
  while (z3_nextm (&it)) Z3_DISCARD_QUAL (free ((void*)it.key));
  free (map->bfs);
}

#endif
