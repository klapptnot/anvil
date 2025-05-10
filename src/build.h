#include <z3_vector.h>
#include <z3_string.h>
#include "config.h"

bool target_needs_rebuild (String *target, Z3Vector dep);

void parse_dependencies (String *rule_str, Z3Vector *dep);

void get_make_dependencies (BuildConfig *config, Z3Vector *deps);

void generate_build_command (BuildConfig *config, Z3Vector *cmd);

void create_object_file (String *target);

void build_target (AnvilConfig *config, String *target, Z3Vector dep);

