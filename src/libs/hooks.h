// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

#pragma once

#include <notrust.h>
#include <stdint.h>
#include <z3_string.h>
#include <z3_toys.h>
#include <z3_vector.h>

#include "config.h"

typedef struct {
  ValidateStr valid;
  CachePolicy cache;
  cstr* command;
} RuntimeHook;

// Load all hooks from folder and config
Vector hooks_get_list (AnvilConfig config);

// Run and get result of a hook by its name.
String hooks_run (String name);

// Get the cached result of a hook by its name.
String hooks_get_cache (String name);

// Set or update the cache for a specific hook.
void hooks_set_cache (String name, String value);

// Get the error message associated with a specific hook.
String hooks_get_error_message (String name);

// Validate the configuration or state of a hook.
bool hooks_validate (String name);

// Drop the cache for a specific hook (if invalid or no longer needed).
void hooks_drop_cache (String name);

// Clear the entire list of hooks (resets all hooks).
void hooks_clear_list (Vector hooks);
