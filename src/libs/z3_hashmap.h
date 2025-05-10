/**
 * z3_hashmap.h
 *
 * Description:
 *   A simple, efficient C hashmap implementation for string key-value pairs.
 *
 * Features:
 *   - String key to string value mapping
 *   - FNV-1a hashing algorithm
 *   - Linear probing for collision resolution
 *   - Automatic memory management for keys and values
 *
 * Requires:
 *   - C99 Standard or higher
 *
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

//~ Hash map structure for string key-value pairs
typedef struct {
  struct Z3HashMapEntry* entries;
  size_t capacity;
  size_t count;
} Z3HashMap;

typedef struct {
  Z3HashMap* map;  // pointer to the map being iterated
  size_t index;    // current index in the table
  char* key;       // current key
  void* val;     // current value
} Z3HashMapIterator;

//~ Create a new empty hash map with default capacity
Z3HashMap* z3_hashmap_create (void);

//~ Insert or update a key-value pair in the hash map
//! Both key and value are duplicated internally
void z3_hashmap_put (Z3HashMap* map, const char* key, void* value);

//~ Retrieve a value by its key
//! Returns NULL if key doesn't exist
const char* z3_hashmap_get (Z3HashMap* map, const char* key);

//~ Remove a key-value pair from the hash map
void z3_hashmap_remove (Z3HashMap* map, const char* key);

//~ Check if a key exists in the hash map
//! Returns non-zero if key exists, zero otherwise
int z3_hashmap_has (Z3HashMap* map, const char* key);

//~ Get the number of key-value pairs in the hash map
size_t z3_hashmap_size (Z3HashMap* map);

//~ Free all memory associated with the hash map
void z3_hashmap_drop (Z3HashMap* map);

//~ Initializes an iterator for the given hash map
//! Must be called before using z3_hashmap_iter_next
void z3_hashmap_iter_init (Z3HashMapIterator* it, Z3HashMap* map);

//~ Advances the iterator to the next valid entry in the map
//! Returns true if an entry is found; false if iteration is complete
bool z3_hashmap_iter_next (Z3HashMapIterator* it);

#ifdef Z3_HASHMAP_IMPL

#define Z3_HASHMAP_INITIAL_CAPACITY 32

struct Z3HashMapEntry {
  char* key;
  void* val;
  uint64_t hash;
  int used;
};

static uint64_t z3__hash_str (const char* str) {
  // FNV-1a hash (classic choice)
  uint64_t hash = 14695981039346656037ULL;
  while (*str) {
    hash ^= (unsigned char)(*str++);
    hash *= 1099511628211ULL;
  }
  return hash;
}

static size_t z3__probe (uint64_t hash, size_t i, size_t cap) {
  return (hash + i) % cap;
}

Z3HashMap* z3_hashmap_create (void) {
  Z3HashMap* map = (Z3HashMap*)malloc (sizeof (Z3HashMap));
  map->capacity = Z3_HASHMAP_INITIAL_CAPACITY;
  map->count = 0;
  map->entries = (struct Z3HashMapEntry*)calloc (map->capacity, sizeof (struct Z3HashMapEntry));
  return map;
}

void z3_hashmap_put (Z3HashMap* map, const char* key, void* value) {
  if (!key || !value) return;

  uint64_t hash = z3__hash_str (key);
  for (size_t i = 0; i < map->capacity; ++i) {
    size_t idx = z3__probe (hash, i, map->capacity);
    struct Z3HashMapEntry* entry = &map->entries[idx];
    if (!entry->used || (entry->key && strcmp (entry->key, key) == 0)) {
      if (!entry->used) map->count++;
      entry->used = 1;
      if (entry->key) free (entry->key);
      if (entry->val) free (entry->val);
      entry->key = strdup (key);
      entry->val = value;
      entry->hash = hash;
      return;
    }
  }
  // TODO: grow here
}

const char* z3_hashmap_get (Z3HashMap* map, const char* key) {
  if (!key) return NULL;
  uint64_t hash = z3__hash_str (key);
  for (size_t i = 0; i < map->capacity; ++i) {
    size_t idx = z3__probe (hash, i, map->capacity);
    struct Z3HashMapEntry* entry = &map->entries[idx];
    if (entry->used && entry->key && strcmp (entry->key, key) == 0) {
      return entry->val;
    }
    if (!entry->used) break;
  }
  return NULL;
}

void z3_hashmap_remove (Z3HashMap* map, const char* key) {
  if (!key) return;
  uint64_t hash = z3__hash_str (key);
  for (size_t i = 0; i < map->capacity; ++i) {
    size_t idx = z3__probe (hash, i, map->capacity);
    struct Z3HashMapEntry* entry = &map->entries[idx];
    if (entry->used && entry->key && strcmp (entry->key, key) == 0) {
      free (entry->key);
      free (entry->val);
      entry->key = NULL;
      entry->val = NULL;
      entry->used = 0;
      map->count--;
      return;
    }
  }
}

int z3_hashmap_has (Z3HashMap* map, const char* key) {
  return z3_hashmap_get (map, key) != NULL;
}

size_t z3_hashmap_size (Z3HashMap* map) {
  return map->count;
}

void z3_hashmap_iter_init (Z3HashMapIterator* it, Z3HashMap* map) {
  it->map = map;
  it->index = 0;
  it->key = NULL;
  it->val = NULL;
}

bool z3_hashmap_iter_next (Z3HashMapIterator* it) {
  while (it->index < it->map->capacity) {
    size_t i = it->index++;
    if (it->map->entries[i].key != NULL) {  // or some kind of "in use" check
      it->key = it->map->entries[i].key;
      it->val = it->map->entries[i].val;
      return true;
    }
  }
  return false;
}

void z3_hashmap_drop (Z3HashMap* map) {
  if (!map) return;
  for (size_t i = 0; i < map->capacity; ++i) {
    struct Z3HashMapEntry* entry = &map->entries[i];
    if (entry->used) {
      free (entry->key);
      free (entry->val);
    }
  }
  free (map->entries);
  free (map);
}

#endif  // Z3_HASHMAP_IMPL
