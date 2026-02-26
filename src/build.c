// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

#include "build.h"

#include <ctype.h>
#include <errno.h>
#include <notrust.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <z3_string.h>
#include <z3_toys.h>
#include <z3_vector.h>

bool target_needs_rebuild (String* target, Vector deps) {
  struct stat target_stat;

  // target doesn't exist
  if (stat ((nstr)target->chr, &target_stat) != 0) {
    return true;
  }

  // target modification time
  time_t target_mtime = target_stat.st_mtime;

  for (usize i = 0; i < deps.len; i++) {
    String* dep = z3_get (deps, i);

    struct stat dep_stat;

    if (stat ((nstr)dep->chr, &dep_stat) != 0) {
      die (
        "Dependency '%s' for target '%s' doesn't exist or can't be accessed: %s\n",
        dep->chr,
        target->chr,
        strerror (errno) // NOLINT(concurrency-mt-unsafe) we are dying- girl
      );
    }

    // dependency newer than target
    if (dep_stat.st_mtime > target_mtime) {
      return true;
    }
  }

  return false;
}

void parse_dependencies (String* rule_str, Vector* deps) {
  usize i = 0;
  while (i < rule_str->len && rule_str->chr[i] != ':') i++;
  if (i >= rule_str->len) return;

  // past the `:`
  i++;

  usize start = 0;
  while (i < rule_str->len) {
    // skip spaces
    while (i < rule_str->len && isspace (rule_str->chr[i])) i++;
    if (i >= rule_str->len) break;

    start = i;
    while (i < rule_str->len && !isspace (rule_str->chr[i])) i++;

    usize len = i - start;
    if (len > 0) {
      String s = z3_str (len);

      z3_pushl (&s, (nstr)&rule_str->chr[start], len);
      z3_push (*deps, s);
    }
  }
}

// void get_make_dependencies (BuildConfig *config, Vector *deps) {
//   //
// }
