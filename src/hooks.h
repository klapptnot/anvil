#pragma once

#include <stdint.h>
#include <z3_toys.h>
#include "config.h"

typedef enum {
  VALIDATE_STR_OFF,
  VALIDATE_STR_COMPACT,
  VALIDATE_STR_CONTENT,
  VALIDATE_STR_STRICT
} ValidateStr;

typedef enum {
  CACHE_POLICY_NEVER,
  CACHE_POLICY_MEMOIZE,
  CACHE_POLICY_ALWAYS
} CachePolicy;

typedef struct {
  String name;
  ValidateStr valid;
  CachePolicy cache;
} RuntimeHook;

typedef struct {
  RuntimeHook* value;
  Vector* chest;
} HashItem;

// linked list of collitions
typedef struct {
  uintptr_t val; // points to the value
  uintptr_t next; // next item
  bool leaf; // if there is a next item
} CollidedItem;

// Load all hooks from folder and config
Vector hooks_get_list(AnvilConfig config);

// Run and get result of a hook by its name.
String hooks_run(String name);

// Get the cached result of a hook by its name.
String hooks_get_cache(String name);

// Set or update the cache for a specific hook.
void hooks_set_cache(String name, String value);

// Get the error message associated with a specific hook.
String hooks_get_error_message(String name);

// Validate the configuration or state of a hook.
bool hooks_validate(String name);

// Drop the cache for a specific hook (if invalid or no longer needed).
void hooks_drop_cache(String name);

// Clear the entire list of hooks (resets all hooks).
void hooks_clear_list(void);

