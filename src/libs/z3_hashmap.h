// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

/**
 * z3_hashmap.h
 *
 * Description:
 *   A memory efficient C hashmap implementation, HashMap<nstr, any*>.
 *
 * Features:
 *   - String key to string value mapping
 *   - FNV-1a hashing algorithm
 *   - Linear probing for collision resolution
 *   - Bit-packed occupation tracking
 *   - Automatic memory management for keys and values
 *
 * Requires:
 *   - C23 Standard (Use -std=c23).
 *   - z3_toys.h
 */
#pragma once

#include <notrust.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <z3_toys.h>

/// @brief A single slot in a HashMap's entry table.
typedef struct {
  u64 hash;    ///< Cached hash of `key`, used to speed up probing/comparison
  nstr key;    ///< Owned, heap-duplicated copy of the key
  void* val;   ///< Pointer to the associated value. Not owned by the map.
} HashMapEntry;

/// @brief Auto-growing HashMap<String, &T>
typedef struct {
  HashMapEntry* beds;  ///< Backing array of entries (open-addressed table)
  usize max;           ///< Current capacity of `beds`
  usize len;           ///< Number of entries currently stored
  usize* bfs;          ///< Bitset/flags tracking occupied/tombstoned slots
} HashMap;

/// @brief Iterator state for walking a HashMap's entries
typedef struct {
  HashMap* map;  ///< Pointer to the map being iterated
  usize idx;     ///< Current index in the table
  nstr key;      ///< Key of the current entry, set after z3_hashmap_iter_next()
  void* val;     ///< Value of the current entry, set after z3_hashmap_iter_next()
} HashMapIterator;

/// @brief Construct a zero-initialized `HashMapIterator` for the given map.
/// @param m Pointer to the `HashMap` to iterate.
/// @return A `HashMapIterator` literal ready to pass to z3_hashmap_iter_next().
#define z3_hashmap_iterator(m)                           \
  (HashMapIterator) {                                    \
    .map = (m), .idx = 0, .key = nullptr, .val = nullptr \
  }

/// @brief Create a new empty hashmap with default capacity
/// @return A newly allocated `HashMap`
HashMap* z3_hashmap_create (void);

/// @brief Insert or update a key-value pair in the hashmap
///
/// The key is duplicated internally, do not inline strdup. Updating a
/// value does NOT replace the key; again, no strdup.
///
/// @param map Pointer to the target `HashMap`
/// @param key The key to insert or update
/// @param value Pointer to the value to associate with `key`
void z3_hashmap_put (HashMap* map, nstr key, void* value);

/// @brief Retrieve a value by its key
/// @param map Pointer to the `HashMap`
/// @param key The key to look up
/// @return The associated value, or `NULL` if the key doesn't exist
void* z3_hashmap_get (HashMap* map, nstr key);

/// @brief Remove a key-value pair from the hashmap
/// @param map Pointer to the `HashMap`
/// @param key The key to remove
void z3_hashmap_remove (HashMap* map, nstr key);

/// @brief Check if a key exists in the hashmap
/// @param map Pointer to the `HashMap`
/// @param key The key to check
/// @return `true` if the key exists, `false` otherwise
bool z3_hashmap_has (HashMap* map, nstr key);

/// @brief Free all memory associated with the hashmap, including the values it owns
/// @param map Pointer to the `HashMap` to clean up
void z3_hashmap_drop (HashMap* map);

/// @brief Free the hashmap and the keys it owns, leaving values orphaned
///
/// Unlike z3_hashmap_drop(), this only releases the map's internal
/// structure and its owned keys — the values are not touched and must
/// be freed by the caller if needed.
///
/// @param map Pointer to the `HashMap` to clean up
void z3_hashmap_drop_shallow (HashMap* map);

/// @brief Initializes an iterator for the given hash map
///
/// Must be called before using z3_hashmap_iter_next().
///
/// @param it Pointer to the `HashMapIterator` to initialize
/// @param map Pointer to the `HashMap` to iterate
void z3_hashmap_iter_init (HashMapIterator* it, HashMap* map);

/// @brief Advances the iterator to the next valid entry in the map
/// @param it Pointer to the `HashMapIterator` to advance
/// @return `true` if an entry was found, `false` if iteration is complete
bool z3_hashmap_iter_next (HashMapIterator* it);

/// @brief Define a HashMap with automatic cleanup via GCC/Clang attribute
#define ScopedHashMap [[gnu::cleanup (z3_hashmap_drop)]] HashMap

#ifdef Z3_HASHMAP_IMPL
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define Z3_HASHMAP_INITIAL_CAPACITY 32
#define Z3_HASHMAP_BITFLAG_CAPACITY (sizeof (usize) * 8)  // bit size
#define Z3_HASHMAP_BITFLAG_SIZE     sizeof (usize)

static u64 z3_hashmap__hash_str (nstr str) {
  // FNV-1a hash
  u64 hash = 14695981039346656037ULL;  // NOLINT(readability-magic-numbers)
  while (*str) {
    hash ^= (u8)(*str++);
    hash *= 1099511628211ULL;  // NOLINT(readability-magic-numbers)
  }
  return hash;
}

static usize z3_hashmap__probe (u64 hash, usize i, usize cap) {
  return (hash + i) % cap;
}

static bool z3_hashmap_pos_used (const usize* bf, usize pos) {
  usize home = pos / Z3_HASHMAP_BITFLAG_CAPACITY;  // Which u64
  u8 room = pos % Z3_HASHMAP_BITFLAG_CAPACITY;     // Which bit in that u64

  return (bool)((bf[home] >> room) & 1);
}

static void z3_hashmap__set_used (usize* bf, usize pos) {
  usize home = pos / Z3_HASHMAP_BITFLAG_CAPACITY;
  u8 room = pos % Z3_HASHMAP_BITFLAG_CAPACITY;

  bf[home] |= (1ULL << room);
}

HashMap* z3_hashmap_create (void) {
  HashMap* map = (HashMap*)malloc (sizeof (HashMap));
  map->max = Z3_HASHMAP_INITIAL_CAPACITY;
  map->len = 0;
  map->beds = (HashMapEntry*)calloc (map->max, sizeof (HashMapEntry));
  map->bfs = calloc (Z3_HASHMAP_BITFLAG_SIZE, 1);
  return map;
}

void z3_hashmap_put (HashMap* map, nstr key, void* value) {
  if (!key || !value) return;

  if (map->len >= map->max * 3 / 4) {
    usize old_capacity = map->max;
    HashMapEntry* old_beds = map->beds;
    usize* exes = map->bfs;

    map->len = 0;
    map->max *= 2;
    map->beds = (HashMapEntry*)calloc (map->max, sizeof (HashMapEntry));
    usize new_cap = /* round up */
      (map->max + (Z3_HASHMAP_BITFLAG_CAPACITY - 1)) / Z3_HASHMAP_BITFLAG_CAPACITY;

    map->bfs = calloc (new_cap * Z3_HASHMAP_BITFLAG_SIZE, 1);
    if (map->bfs == nullptr) die ("failed to allocate bitflag maps");

    // rehash all existing entries
    for (usize i = 0; i < old_capacity; ++i) {
      if (old_beds[i].key != nullptr) {
        u64 hash = old_beds[i].hash;
        for (usize j = 0; j < map->max; ++j) {
          usize idx = z3_hashmap__probe (hash, j, map->max);
          if (!z3_hashmap_pos_used (map->bfs, idx)) {
            HashMapEntry* entry = &map->beds[idx];

            entry->key = old_beds[i].key;
            entry->val = old_beds[i].val;
            entry->hash = hash;
            map->len++;

            z3_hashmap__set_used (map->bfs, idx);
            break;
          }
        }
      }
    }
    free (old_beds);
    free (exes);
  }

  u64 hash = z3_hashmap__hash_str (key);
  for (usize i = 0; i < map->max; ++i) {
    usize idx = z3_hashmap__probe (hash, i, map->max);
    HashMapEntry* entry = &map->beds[idx];
    bool is_used = z3_hashmap_pos_used (map->bfs, idx);
    if (!is_used || (entry->key && strcmp (entry->key, key) == 0)) {
      z3_hashmap__set_used (map->bfs, idx);
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

void* z3_hashmap_get (HashMap* map, nstr key) {
  if (!key) return nullptr;
  u64 hash = z3_hashmap__hash_str (key);
  for (usize i = 0; i < map->max; ++i) {
    usize idx = z3_hashmap__probe (hash, i, map->max);

    HashMapEntry* entry = &map->beds[idx];
    bool is_used = z3_hashmap_pos_used (map->bfs, idx);
    if ((i32)is_used && (entry->key && strcmp (entry->key, key) == 0)) {
      return entry->val;
    }

    if (!is_used) break;
  }
  return nullptr;
}

void z3_hashmap_remove (HashMap* map, nstr key) {
  if (!key) return;
  u64 hash = z3_hashmap__hash_str (key);
  for (usize i = 0; i < map->max; ++i) {
    usize idx = z3_hashmap__probe (hash, i, map->max);
    HashMapEntry* entry = &map->beds[idx];
    bool is_used = z3_hashmap_pos_used (map->bfs, idx);
    if ((i32)is_used && (entry->key && strcmp (entry->key, key) == 0)) {
      // never setting pos_used entry to false
      // if (is_used && key == nullptr) /* was deleted, free tombstone */
      // very few changes needed

      KILL_CAST_QUAL (free ((void*)entry->key);)
      free (entry->val);
      entry->key = nullptr;
      entry->val = nullptr;

      map->len--;
      return;
    }
  }
}

inline bool z3_hashmap_has (HashMap* map, nstr key) {
  return z3_hashmap_get (map, key) != nullptr;
}

void z3_hashmap_iter_init (HashMapIterator* it, HashMap* map) {
  it->map = map;
  it->idx = 0;
  it->key = nullptr;
  it->val = nullptr;
}

bool z3_hashmap_iter_next (HashMapIterator* it) {
  while (it->idx < it->map->max) {
    usize i = it->idx++;

    if (z3_hashmap_pos_used (it->map->bfs, i) && it->map->beds[i].key) {
      it->key = it->map->beds[i].key;
      it->val = it->map->beds[i].val;
      return true;
    }
  }
  return false;
}

void z3_hashmap_drop (HashMap* map) {
  if (!map) return;
  for (usize i = 0; i < map->max; ++i) {
    if (z3_hashmap_pos_used (map->bfs, i)) {
      HashMapEntry* entry = &map->beds[i];
      if (entry->key) {
        KILL_CAST_QUAL (free ((void*)entry->key);)
        free (entry->val);
      }
    }
  }
  free (map->beds);
  free (map->bfs);
  free (map);
}

void z3_hashmap_drop_shallow (HashMap* map) {
  if (!map) return;
  for (usize i = 0; i < map->max; ++i) {
    if (z3_hashmap_pos_used (map->bfs, i)) {
      HashMapEntry* entry = &map->beds[i];
      KILL_CAST_QUAL (if (entry->key) free ((void*)entry->key);)
    }
  }
  free (map->beds);
  free (map->bfs);
  free (map);
}

#endif  // Z3_HASHMAP_IMPL
