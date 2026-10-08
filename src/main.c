// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

#include <notrust.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define Z3_TOYS_SCOPED
#define Z3_TOYS_IMPL
#define Z3_MEM_IMPL
#define Z3_STRING_IMPL
#define Z3_HASHMAP_IMPL
#define Z3_VECTOR_IMPL
#include <config.h>
#include <yaml.h>
#include <z3_hashmap.h>
#include <z3_mem.h>
#include <z3_string.h>
#include <z3_toys.h>
#include <z3_vector.h>

[[maybe_unused]]
static void z3__display_String (String* val) {
  OwnedString s = z3_escape (val->chr, val->len);
  printf ("{ len: %2zu, max: %2zu } \"", s.len, s.max);
  (void)fflush (stdout);
  z3_prints (s);
  putchar ('"');
}

static void print_anvil_config (AnvilConfig* config) {
  if (!config) {
    printf ("AnvilConfig is nullptr\n");
    return;
  }

  printf ("=== AnvilConfig ===\n");
  printf ("Package: %s\n", config->package);
  printf ("Version: %s\n", config->version);
  printf ("Author: %s\n", config->author);
  printf ("Description: %s\n", config->description);

  // Workspace
  printf ("\n-- Workspace --\n");
  printf ("Libs Path: %s\n", config->workspace->libs);
  printf ("Build Path: %s\n", config->workspace->build);

  // Targets
  printf ("\n-- Targets -- %zu\n", config->targets->count);
  if (config->targets) {
    for (usize i = 0; i < config->targets->count; i++) {
      TargetConfig* tgt = config->targets->target[i];
      printf ("Target %zu:\n", i);
      printf ("  Name: %s\n", tgt->name);
      printf ("  Type: %s\n", tgt->type);
      printf ("  Main: %s\n", tgt->main);
      for (usize j = 0; j < tgt->target_count; j++) {
        printf ("    for[%zu]: %s\n", j, tgt->target[j]);
      }
    }
  } else {
    printf ("No targets defined.\n");
  }

  // Build
  printf ("\n-- Build --\n");
  printf ("Compiler   = %s\n", config->build->compiler);
  printf ("C Standard = %s\n", config->build->cstd);
  printf ("Jobs       = %d\n", config->build->jobs);

  printf ("Macros:\n");
  {
    HashMapIterator it = z3_iterm (&config->build->macros);
    while (z3_nextm (&it)) {
      printf ("  %s = %s\n", it.key, (char*)it.val);
    }
  }

  printf ("Arguments:\n");
  {
    HashMapIterator it = z3_iterm (&config->build->arguments);
    OwnedString command_line =
      z3_str (32);  // NOLINT (readability-magic-numbers)
    while (z3_nextm (&it)) {
      printf ("  %s\n", it.key);
      ArgumentConfig* args = it.val;
      printf ("    validation   = %d\n", args->validation);
      printf ("    cache_policy = %d\n", args->cache_policy);
      for (usize i = 0; i < args->command_len; i++) {
        usize len = strlen ((cnstr)args->command[i]);
        z3_pushl (&command_line, args->command[i], len);
        if (i < args->command_len - 1) z3_pushc (&command_line, ' ');
      }
      printf ("    :~> %s\n", command_line.chr);
      command_line.len = 0;
    }
  }

  printf ("Dependencies:\n");
  for (usize i = 0; i < config->build->deps_count; i++) {
    DependencyConfig dep = config->build->deps[i];
    printf ("  Dependency %zu:\n", i);
    printf ("    Name: %s\n", dep.name);
    printf ("    Type: %s\n", dep.type);
    printf ("    Repo: %s\n", dep.repo);
    printf ("    Path: %s\n", dep.path);
  }

  {
    printf ("\n-- Profiles --\n");
    HashMapIterator it = z3_iterm (&config->profiles);
    while (z3_nextm (&it)) {
      Vector* profc = it.val;
      printf ("  %s (%zu):\n", it.key, profc->len);
      for (usize i = 0; i < profc->len; i++) {
        char* s = z3_getvp (*profc, i);
        printf ("      [%zu] %s\n", i, s);
      }
    }
  }

  printf ("====================\n");
}

i32 main (i32 argc, cnstr* argv) {
  // u8* file = __anvil_hook ("load-bytes", "hooks/load-bytes");
  // printf ("# load-bytes\n%s", file);
  (void)popf (argc, argv);              // NOLINT(concurrency-mt-unsafe)
  cnstr file_name = popf (argc, argv);  // NOLINT(concurrency-mt-unsafe)

  YamlStore store = {0};
  z3_register (&store.str_pools, z3_dropv);
  z3_register (&store.owned_strs, z3_dropv);

  Node* root = parse_yaml (file_name, &store);
  if (!root) die ("Empty YAML config file");
  z3_register (root, free_yaml);

  AnvilConfig* config = dset_anvil_config (root);
  z3_register (config, free_anvil_config);

  print_anvil_config (config);

  z3_drop ();
  return 0;
}
