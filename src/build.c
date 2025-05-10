#include <bits/types/time_t.h>
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <z3_string.h>
#include <z3_vector.h>

bool target_needs_rebuild (String *target, Z3Vector deps) {
  struct stat target_stat;

  // target doesn't exist
  if (stat (target->chr, &target_stat) != 0) {
    return true;
  }

  // target modification time
  time_t target_mtime = target_stat.st_mtime;

  for (size_t i = 0; i < deps.len; i++) {
    String *dep = z3_get (deps, i);

    struct stat dep_stat;

    if (stat (dep->chr, &dep_stat) != 0) {
      eprintf (
          "Dependency '%s' for target '%s' doesn't exist or can't be accessed: %s\n", dep->chr,
          target->chr, strerror (errno)
      );
      exit (1);
    }

    // dependency newer than target
    if (dep_stat.st_mtime > target_mtime) {
      return true;
    }
  }

  return false;
}

void parse_dependencies (String *str, Z3Vector *deps) {
  size_t i = 0;
  while (i < str->len && str->chr[i] != ':') i++;
  if (i >= str->len) return;

  // past the `:`
  i++;

  size_t start = 0;
  while (i < str->len) {
    // skip spaces
    while (i < str->len && isspace (str->chr[i])) i++;
    if (i >= str->len) break;

    start = i;
    while (i < str->len && !isspace (str->chr[i])) i++;

    size_t len = i - start;
    if (len > 0) {
      String s = z3_str (len);

      z3_pushl (&s, &str->chr[start], len);
      z3_push (*deps, s);
    }
  }
}
