// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

#include <z3_vector.h>
#include <z3_string.h>
#include "config.h"

bool target_needs_rebuild (String *target, VectorZ3 deps);
void parse_dependencies (String *rule_str, VectorZ3 *deps);
void get_make_dependencies (BuildConfig *config, VectorZ3 *deps);
void generate_build_command (BuildConfig *config, VectorZ3 *cmd);
void create_object_file (String *target);
void build_target (AnvilConfig *config, String *target, VectorZ3 deps);

