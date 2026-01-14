// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

/**
 * z3_hashmap.h
 *
 * Description:
 *   A memory efficient C hashmap implementation, HashMap<char*, any*>.
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

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <z3_toys.h>

typedef struct {
  uint64_t hash;
  char* key;
  void* val;
} Z3HashMapEntry;

//~ Auto-growing HashMap<String, &T>
typedef struct {
  Z3HashMapEntry* beds;
  size_t max;
  size_t len;
  size_t* bfs;
} Z3HashMap;

typedef struct {
  Z3HashMap* map;  // pointer to the map being iterated
  size_t idx;      // current index in the table
  char* key;       // current key
  void* val;       // current value
} Z3HashMapIterator;

#define z3_hashmap_iterator(m)                           \
  (Z3HashMapIterator) {                                  \
    .map = (m), .idx = 0, .key = nullptr, .val = nullptr \
  }

//~ Create a new empty hashmap with default capacity
Z3HashMap* z3_hashmap_create (void);

//~ Insert or update a key-value pair in the hashmap
//! The key is duplicated internally, do not inline strdup
//! Updating a value does NOT replace the key; again, no strdup
void z3_hashmap_put (Z3HashMap* map, const char* key, void* value);

//~ Retrieve a value by its key
//! Returns NULL if key doesn't exist
const char* z3_hashmap_get (Z3HashMap* map, const char* key);

//~ Remove a key-value pair from the hashmap
void z3_hashmap_remove (Z3HashMap* map, const char* key);

//~ Check if a key exists in the hashmap
//! Returns non-zero if key exists, zero otherwise
bool z3_hashmap_has (Z3HashMap* map, const char* key);

//~ Free all memory associated with the hashmap
void z3_hashmap_drop (Z3HashMap* map);

//~ Free the hashmap memory, leaving values orphaned
void z3_hashmap_drop_shallow (Z3HashMap* map);

//~ Initializes an iterator for the given hash map
//! Must be called before using z3_hashmap_iter_next
void z3_hashmap_iter_init (Z3HashMapIterator* it, Z3HashMap* map);

//~ Advances the iterator to the next valid entry in the map
//! Returns true if an entry is found; false if iteration is complete
bool z3_hashmap_iter_next (Z3HashMapIterator* it);

#ifdef Z3_HASHMAP_IMPL

#define Z3_HASHMAP_INITIAL_CAPACITY 32
#define Z3_HASHMAP_BITFLAG_CAPACITY (sizeof (size_t) * 8)  // bit size
#define Z3_HASHMAP_BITFLAG_SIZE     sizeof (size_t)

static uint64_t z3_hashmap__hash_str (const char* str) {
  // FNV-1a hash
  uint64_t hash = 14695981039346656037ULL;  // NOLINT (readability-magic-numbers)
  while (*str) {
    hash ^= (unsigned char)(*str++);
    hash *= 1099511628211ULL;  // NOLINT (readability-magic-numbers)
  }
  return hash;
}

static size_t z3_hashmap__probe (uint64_t hash, size_t i, size_t cap) {
  return (hash + i) % cap;
}

static bool z3_hashmap_pos_used (const size_t* bf, size_t pos) {
  size_t home = pos / Z3_HASHMAP_BITFLAG_CAPACITY;   // Which u64
  uint8_t room = pos % Z3_HASHMAP_BITFLAG_CAPACITY;  // Which bit in that u64

  return (bool)((bf[home] >> room) & 1);
}

static void z3_hashmap_set_used (size_t* bf, size_t pos, bool val) {
  size_t home = pos / Z3_HASHMAP_BITFLAG_CAPACITY;
  uint8_t room = pos % Z3_HASHMAP_BITFLAG_CAPACITY;

  if (val) {
    bf[home] |= (1ULL << room);
  } else {
    bf[home] &= ~(1ULL << room);
  }
}

Z3HashMap* z3_hashmap_create (void) {
  Z3HashMap* map = (Z3HashMap*)malloc (sizeof (Z3HashMap));
  map->max = Z3_HASHMAP_INITIAL_CAPACITY;
  map->len = 0;
  map->beds = (Z3HashMapEntry*)calloc (map->max, sizeof (Z3HashMapEntry));
  map->bfs = calloc (Z3_HASHMAP_BITFLAG_SIZE, 1);
  return map;
}

void z3_hashmap_put (Z3HashMap* map, const char* key, void* value) {
  if (!key || !value) return;

  if (map->len >= map->max * 3 / 4) {
    size_t old_capacity = map->max;
    Z3HashMapEntry* old_beds = map->beds;
    size_t* exes = map->bfs;

    map->len = 0;
    map->max *= 2;
    map->beds = (Z3HashMapEntry*)calloc (map->max, sizeof (Z3HashMapEntry));
    size_t new_cap = /* round up */
      (map->max + (Z3_HASHMAP_BITFLAG_CAPACITY - 1)) / Z3_HASHMAP_BITFLAG_CAPACITY;

    map->bfs = calloc (new_cap * Z3_HASHMAP_BITFLAG_SIZE, 1);
    if (map->bfs == nullptr) die ("failed to allocate bitflag maps\n");

    // rehash all existing entries
    for (size_t i = 0; i < old_capacity; ++i) {
      if ((int)z3_hashmap_pos_used (exes, i) && old_beds[i].key) {
        uint64_t hash = old_beds[i].hash;
        for (size_t j = 0; j < map->max; ++j) {
          size_t idx = z3_hashmap__probe (hash, j, map->max);
          if (!z3_hashmap_pos_used (map->bfs, idx)) {
            Z3HashMapEntry* entry = &map->beds[idx];
            entry->key = old_beds[i].key;
            entry->val = old_beds[i].val;
            entry->hash = hash;
            map->len++;
            z3_hashmap_set_used (map->bfs, idx, true);
            break;
          }
        }
      }
    }
    free (old_beds);
    free (exes);
  }

  uint64_t hash = z3_hashmap__hash_str (key);
  for (size_t i = 0; i < map->max; ++i) {
    size_t idx = z3_hashmap__probe (hash, i, map->max);
    Z3HashMapEntry* entry = &map->beds[idx];
    bool is_used = z3_hashmap_pos_used (map->bfs, idx);
    if (!is_used || (entry->key && strcmp (entry->key, key) == 0)) {
      z3_hashmap_set_used (map->bfs, idx, true);
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

const char* z3_hashmap_get (Z3HashMap* map, const char* key) {
  if (!key) return nullptr;
  uint64_t hash = z3_hashmap__hash_str (key);
  for (size_t i = 0; i < map->max; ++i) {
    size_t idx = z3_hashmap__probe (hash, i, map->max);
    Z3HashMapEntry* entry = &map->beds[idx];
    bool is_used = z3_hashmap_pos_used (map->bfs, idx);
    if ((int)is_used && (entry->key && strcmp (entry->key, key) == 0)) {
      return entry->val;
    }
    if (!is_used) break;
  }
  return nullptr;
}

void z3_hashmap_remove (Z3HashMap* map, const char* key) {
  if (!key) return;
  uint64_t hash = z3_hashmap__hash_str (key);
  for (size_t i = 0; i < map->max; ++i) {
    size_t idx = z3_hashmap__probe (hash, i, map->max);
    Z3HashMapEntry* entry = &map->beds[idx];
    bool is_used = z3_hashmap_pos_used (map->bfs, idx);
    if ((int)is_used && (entry->key && strcmp (entry->key, key) == 0)) {
      z3_hashmap_set_used (map->bfs, idx, false);
      free (entry->key);
      free (entry->val);
      entry->key = nullptr;
      entry->val = nullptr;
      map->len--;
      return;
    }
  }
}

bool z3_hashmap_has (Z3HashMap* map, const char* key) {
  return z3_hashmap_get (map, key) != nullptr;
}

void z3_hashmap_iter_init (Z3HashMapIterator* it, Z3HashMap* map) {
  it->map = map;
  it->idx = 0;
  it->key = nullptr;
  it->val = nullptr;
}

bool z3_hashmap_iter_next (Z3HashMapIterator* it) {
  while (it->idx < it->map->max) {
    size_t i = it->idx++;

    if (z3_hashmap_pos_used (it->map->bfs, i)) {
      it->key = it->map->beds[i].key;
      it->val = it->map->beds[i].val;
      return true;
    }
  }
  return false;
}

void z3_hashmap_drop (Z3HashMap* map) {
  if (!map) return;
  for (size_t i = 0; i < map->max; ++i) {
    if (z3_hashmap_pos_used (map->bfs, i)) {
      Z3HashMapEntry* entry = &map->beds[i];
      free (entry->key);
      free (entry->val);
    }
  }
  free (map->beds);
  free (map->bfs);
  free (map);
}

void z3_hashmap_drop_shallow (Z3HashMap* map) {
  if (!map) return;
  for (size_t i = 0; i < map->max; ++i) {
    if (z3_hashmap_pos_used (map->bfs, i)) {
      Z3HashMapEntry* entry = &map->beds[i];
      free (entry->key);
    }
  }
  free (map->beds);
  free (map->bfs);
  free (map);
}

#endif  // Z3_HASHMAP_IMPL
