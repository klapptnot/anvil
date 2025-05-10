#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define Z3_TOYS_SCOPED
#define Z3_TOYS_IMPL
#define Z3_STRING_IMPL
#define Z3_HASHMAP_IMPL
#define Z3_VECTOR_IMPL
#include <z3_hashmap.h>
#include <z3_string.h>
#include <z3_toys.h>
#include <z3_vector.h>

#include "build.h"
#include "config.h"
#include "yaml.h"

String int_to_str (int num);
String float_to_str (float num);

// z3_dropfn (String, z3_drops);

bool fill (String* s, void* ctx, char* path, size_t) {
  Z3Vector* any = ctx;
  size_t i = atoi (path);
  if (i > any->len) return false;
  String* as = z3_get (*any, i);
  z3_pushl (s, as->chr, as->len);
  return true;
}

#define __dynarray_print_String(val)                           \
  {                                                            \
    String s = z3_escape ((val)->chr, (val)->len);             \
    printf ("{l: %2zu, m: %2zu} \"%s\"", s.len, s.max, s.chr); \
    z3_drops (&s);                                             \
  }
void print_anvil_config (const AnvilConfig* config) {
  if (!config) {
    printf ("AnvilConfig is NULL\n");
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
  printf ("Target Path: %s\n", config->workspace->target);

  // Targets
  printf ("\n-- Targets -- %zu\n", config->targets->count);
  if (config->targets) {
    for (size_t i = 0; i < config->targets->count; i++) {
      TargetConfig* tgt = config->targets->target[i];
      printf ("Target %zu:\n", i);
      printf ("  Name: %s\n", tgt->name);
      printf ("  Type: %s\n", tgt->type);
      printf ("  Main: %s\n", tgt->main);
      for (size_t j = 0; j < tgt->target_count; j++) {
        printf ("    arch[%zu]: %s\n", j, tgt->target[j]);
      }
    }
  } else {
    printf ("No targets defined.\n");
  }

  // Build
  printf ("\n-- Build --\n");
  printf ("Compiler: %s\n", config->build->compiler);
  printf ("C Standard: %s\n", config->build->cstd);

  printf ("Macros:\n");
  if (config->build->macros) {
    Z3HashMapIterator it;
    z3_hashmap_iter_init (&it, config->build->macros);
    while (z3_hashmap_iter_next (&it)) {
      printf ("  %s = %s\n", it.key, (char*)it.val);
    }
  }

  printf ("Arguments:\n");
  if (config->build->arguments) {
    Z3HashMapIterator it;
    z3_hashmap_iter_init (&it, config->build->arguments);
    while (z3_hashmap_iter_next (&it)) {
      printf ("  %s\n", it.key);
      ArgumentConfig* args = it.val;
      printf ("    validate_str = %s\n", args->validate_str);
      printf ("    cache_policy = %s\n", args->cache_policy);
      for (size_t i = 0; i < args->commands_count; i++) {
        printf ("    -> %s\n", args->commands[i]);
      }
    }
  }

  printf ("Dependencies:\n");
  for (size_t i = 0; i < config->build->deps_count; i++) {
    DependencyConfig dep = config->build->deps[i];
    printf ("  Dependency %zu:\n", i);
    printf ("    Name: %s\n", dep.name);
    printf ("    Type: %s\n", dep.type);
    printf ("    Repo: %s\n", dep.repo);
    printf ("    Path: %s\n", dep.path);
  }

  // Profiles
  if (config->profiles) {
    printf ("\n-- Profiles --\n");
    Z3HashMapIterator it;
    z3_hashmap_iter_init (&it, config->profiles);
    while (z3_hashmap_iter_next (&it)) {
      Z3Vector* profc = it.val;
      printf ("  %s (%zu):\n", it.key, profc->len);
      for (size_t i = 0; i < profc->len; i++) {
        printf ("      [%zu] %s\n", i, *(char**)z3_get (*profc, i));
      }
    }
  }

  printf ("====================\n");
}

int main (int argc, char** argv) {
  IGNORE_UNUSED (char* _this_file = popf (argc, argv));
  char* file_name = popf (argc, argv);

  FILE* file = fopen (file_name, "r");
  fseek (file, 0, SEEK_END);
  long length = ftell (file);
  fseek (file, 0, SEEK_SET);

  char* yaml_input = malloc (next_power_of2 (length + 1));
  IGNORE_UNUSED (fread (yaml_input, 1, length, file));
  yaml_input[length] = '\0';
  fclose (file);

  Node* root = parse_yaml (yaml_input);

  if (!root) {
    eprintf ("Failed to parse YAML\n");
    return 1;
  }

  AnvilConfig* config = malloc (sizeof (AnvilConfig));
  dset_anvil_config (config, root);
  print_anvil_config (config);

  free_yaml (root);

  free (yaml_input);
  return 0;
}

#ifdef _IGNORE
#include <math.h>
String int_to_str (int num) {
  String str = z3_str (16);

  if (num < 0) {
    z3_pushc (&str, '-');
    num = -num;
  }

  if (num == 0) {
    z3_pushc (&str, '0');
    return str;
  }

  char digits[16];
  int i = 0;

  while (num > 0 && i < 16) {
    digits[i++] = '0' + (num % 10);
    num /= 10;
  }

  while (i-- > 0) {
    z3_pushc (&str, digits[i]);
  }

  return str;
}

String float_to_str (float num) {
  String str = z3_str (32);

  if (num == INFINITY) {
    z3_pushl (&str, "inf", 3);
    return str;
  } else if (num == -INFINITY) {
    z3_pushl (&str, "-inf", 4);
    return str;
  } else if (num == 0) {
    z3_pushc (&str, '0');
    return str;
  }

  if (num < 0) {
    z3_pushc (&str, '-');
    num = -num;
  }

  int int_part = (int)num;
  float frac_part = num - int_part;

  if (int_part == 0) {
    z3_pushc (&str, '0');
  } else {
    char digits[16];
    int i = 0, temp = int_part;
    while (temp > 0 && i < 16) {
      digits[i++] = '0' + (temp % 10);
      temp /= 10;
    }
    while (i-- > 0) {
      z3_pushc (&str, digits[i]);
    }
  }

  if (frac_part > 0) {
    z3_pushc (&str, '.');

    int frac_int = 1;
    char frac_digits[8];
    int j = 0, first_zero = -1;

    // Up to 7 significant places max
    for (int i = 0; i < 7; i++) {
      frac_part *= 10;
      frac_int = (int)(frac_part + 0.5);

      int digit = frac_int % 10;
      frac_digits[j++] = '0' + digit;

      if (digit == 0) {
        first_zero = j;
      }
    }

    for (int i = 0; i < first_zero; i++) {
      z3_pushc (&str, frac_digits[i]);
    }
  }

  return str;
}
#endif
