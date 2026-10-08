// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

#include <config.h>
#include <notrust.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <yaml.h>
#include <z3_hashmap.h>
#include <z3_mem.h>
#include <z3_toys.h>
#include <z3_vector.h>

static ValidateStr get_validation_policy (Node* vstr) {
  if (!vstr || vstr->kind != NODE_STRING) return VALIDATE_NONE;
  cnstr s = (cnstr)vstr->string;
  if (strcmp (s, "none") == 0) return VALIDATE_NONE;
  if (strcmp (s, "status") == 0) return VALIDATE_STATUS;
  if (strcmp (s, "content") == 0) return VALIDATE_CONTENT;
  if (strcmp (s, "all") == 0) return VALIDATE_ALL;
  return VALIDATE_NONE;
}

static CachePolicy get_cache_policy (Node* cply) {
  if (!cply || cply->kind != NODE_STRING) return CACHE_POLICY_NEVER;
  cnstr s = (cnstr)cply->string;
  if (strcmp (s, "never") == 0) return CACHE_POLICY_NEVER;
  if (strcmp (s, "memoize") == 0) return CACHE_POLICY_MEMOIZE;
  if (strcmp (s, "always") == 0) return CACHE_POLICY_ALWAYS;
  return CACHE_POLICY_NEVER;
}

void dset_argument_config (ArgumentConfig* acon, Node* node) {
  if (!node || node->kind != NODE_MAP) return;
  Node* cmds = map_get_node (node, "command");
  if (cmds && cmds->kind == NODE_LIST) return;

  Node* vstr = map_get_node (node, "validation");
  acon->validation = get_validation_policy (vstr);

  Node* cpol = map_get_node (node, "cache_policy");
  acon->cache_policy = get_cache_policy (cpol);


  acon->command_len = cmds->list.size;
  acon->command = (const u8**)malloc (sizeof (char*) * acon->command_len);
  for (size_t i = 0; i < acon->command_len; i++) {
    Node* item = cmds->list.items[i];
    acon->command[i] =
      (item && item->kind == NODE_STRING) ? item->string : nullptr;
  }
}

void dset_dependency_config (DependencyConfig* dcon, Node* node) {
  if (!node || node->kind != NODE_MAP) return;

  Node* name = map_get_node (node, "name");
  dcon->name = (name && name->kind == NODE_STRING) ? name->string : nullptr;

  Node* type = map_get_node (node, "type");
  dcon->type = (type && type->kind == NODE_STRING) ? type->string : nullptr;

  Node* repo = map_get_node (node, "repo");
  dcon->repo = (repo && repo->kind == NODE_STRING) ? repo->string : nullptr;

  Node* path = map_get_node (node, "path");
  dcon->path = (path && path->kind == NODE_STRING) ? path->string : nullptr;
}

WorkspaceConfig* dset_workspace_config (Node* node) {
  if (!node || node->kind != NODE_MAP) return nullptr;
  WorkspaceConfig* wconf = malloc (sizeof (WorkspaceConfig));

  Node* wlibs = map_get_node (node, "libs");
  if (wlibs && wlibs->kind == NODE_STRING) {
    wconf->libs = wlibs->string;
  } else {
    wconf->libs = (czstr)DEFAULT_LIBS_PATH;
  }

  Node* wtarget = map_get_node (node, "build");
  if (wtarget && wtarget->kind == NODE_STRING) {
    wconf->build = wtarget->string;
  } else {
    wconf->build = (czstr)DEFAULT_TARGET_PATH;
  }

  return wconf;
}

void dset_profile_config (HashMap* pconf, Node* node) {
  if (!node || node->kind != NODE_MAP) return;

  for (size_t i = 0; i < node->map.size; ++i) {
    czstr key = node->map.entries[i].key;
    Node* val = node->map.entries[i].val;

    if (!key || !val || val->kind != NODE_LIST) continue;

    Vector* flags = z3_malloc (sizeof (Vector));
    *flags = z3_vec (nstr, nullptr);

    for (size_t j = 0; j < val->list.size; j++) {
      Node* vi = val->list.items[j];
      if (vi && vi->kind == NODE_STRING) {
        z3_addv (flags, (void*)&vi->string);
      }
    }

    z3_addm (pconf, (cnstr)key, flags);
  }
}

void dset_target_config (BuildTarget* tconf, Node* node) {
  if (!node || node->kind != NODE_LIST) return;
  if (node->list.size == 0) {
    tconf->count = 0;
    tconf->target = nullptr;
    return;
  }

  tconf->count = node->list.size;
  tconf->target =
    (TargetConfig**)malloc (sizeof (TargetConfig) * node->list.size);
  for (size_t i = 0; i < node->list.size; i++) {
    TargetConfig* tari = malloc (sizeof (TargetConfig));
    Node* tnode = node->list.items[i];
    Node* name = map_get_node (tnode, "name");
    tari->name = (name && name->kind == NODE_STRING) ? name->string : nullptr;

    Node* type = map_get_node (tnode, "type");
    tari->type = (type && type->kind == NODE_STRING) ? type->string : nullptr;

    Node* main = map_get_node (tnode, "main");
    tari->main = (main && main->kind == NODE_STRING) ? main->string : nullptr;

    tnode = map_get_node (tnode, "for");
    if (tnode && tnode->kind == NODE_LIST) {
      tari->target_count = tnode->list.size;
      tari->target = (const u8**)malloc (sizeof (char*) * tnode->list.size);
      for (size_t j = 0; j < tnode->list.size; ++j) {
        Node* elem = tnode->list.items[j];
        tari->target[j] =
          (elem && elem->kind == NODE_STRING) ? elem->string : nullptr;
      }
    } else {
      tari->target = nullptr;
      tari->target_count = 0;
    }
    tconf->target[i] = tari;
  }
}

void dset_build_config (BuildConfig* bconf, Node* node) {
  Node* comp = map_get_node (node, "compiler");
  bconf->compiler =
    (comp && comp->kind == NODE_STRING) ? comp->string : nullptr;

  Node* std = map_get_node (node, "cstd");
  bconf->cstd = (std && std->kind == NODE_STRING) ? std->string : nullptr;

  Node* jobs = map_get_node (node, "jobs");
  bconf->jobs = (jobs && jobs->kind == NODE_NUMBER) ? (u32)jobs->number : 0;

  // --- macros hashmap ---
  Node* macros = map_get_node (node, "macros");
  if (macros && macros->kind == NODE_MAP) {
    bconf->macros = z3_map (nullptr);
    for (size_t i = 0; i < macros->map.size; ++i) {
      czstr key = macros->map.entries[i].key;
      Node* val = macros->map.entries[i].val;
      if (key && val && val->kind == NODE_STRING) {
        z3_addm (&bconf->macros, (cnstr)key, Z3_DISCARD_QUAL (val->string));
      }
    }
  }

  // --- arguments hashmap ---
  Node* args = map_get_node (node, "arguments");
  if (args && args->kind == NODE_MAP) {
    bconf->arguments = z3_map (free);
    for (size_t i = 0; i < args->map.size; ++i) {
      czstr key = args->map.entries[i].key;
      Node* val = args->map.entries[i].val;
      if (key && val && val->kind == NODE_MAP) {
        ArgumentConfig* argconf = malloc (sizeof (ArgumentConfig));
        dset_argument_config (argconf, val);
        z3_addm (&bconf->arguments, (cnstr)key, (void*)argconf);
      }
    }
  }

  // --- deps ---
  Node* deps = map_get_node (node, "deps");
  if (deps && deps->kind == NODE_LIST) {
    bconf->deps_count = (u32)deps->list.size;
    bconf->deps = malloc (sizeof (DependencyConfig) * bconf->deps_count);
    for (size_t i = 0; i < bconf->deps_count; ++i) {
      dset_dependency_config (&bconf->deps[i], deps->list.items[i]);
    }
  } else {
    bconf->deps = nullptr;
    bconf->deps_count = 0;
  }
}

AnvilConfig* dset_anvil_config (Node* node) {
  if (!node || node->kind != NODE_MAP) return nullptr;
  AnvilConfig* conf = malloc (sizeof (AnvilConfig));

  Node* pkg = map_get_node (node, "package");
  conf->package = (pkg && pkg->kind == NODE_STRING) ? pkg->string : nullptr;

  Node* ver = map_get_node (node, "version");
  conf->version = (ver && ver->kind == NODE_STRING) ? ver->string : nullptr;

  Node* auth = map_get_node (node, "author");
  conf->author = (auth && auth->kind == NODE_STRING) ? auth->string : nullptr;

  Node* desc = map_get_node (node, "description");
  conf->description =
    (desc && desc->kind == NODE_STRING) ? desc->string : nullptr;

  // --- Workspace ---
  Node* workspc = map_get_node (node, "workspace");
  if (nullptr != workspc && workspc->kind == NODE_MAP) {
    conf->workspace = dset_workspace_config (workspc);
  } else {
    conf->workspace = nullptr;
  }

  // --- Targets ---
  Node* targets_node = map_get_node (node, "targets");
  if (targets_node && targets_node->kind == NODE_LIST) {
    conf->targets = malloc (sizeof (BuildTarget));
    dset_target_config (conf->targets, targets_node);
  } else {
    conf->targets = nullptr;
  }

  // --- Build Config ---
  Node* build_node = map_get_node (node, "build");
  if (build_node && build_node->kind == NODE_MAP) {
    conf->build = malloc (sizeof (BuildConfig));
    dset_build_config (conf->build, build_node);
  } else {
    conf->build = nullptr;
  }

  // --- Profiles ---
  Node* profiles_node = map_get_node (node, "profiles");
  if (profiles_node && profiles_node->kind == NODE_MAP) {
    conf->profiles = z3_map (z3_leakv);
    dset_profile_config (&conf->profiles, profiles_node);
  }

  return conf;
}

void free_target_config (BuildTarget* tconf) {
  if (!tconf) return;

  if (tconf->target) {
    for (size_t i = 0; i < tconf->count; i++) {
      TargetConfig* tari = tconf->target[i];
      if (tari) {
        // name, type, main are owned by Node tree
        if (tari->target) {
          // target array elements are owned by Node tree
          free ((void*)tari->target);
        }
        free (tari);
      }
    }
    free ((void*)tconf->target);
  }

  free (tconf);
}

void free_profile_config (HashMap* pconf) {
  z3_dropm (pconf);
}

void free_build_config (BuildConfig* bconf) {
  if (!bconf) return;

  // compiler and cstd are owned by Node tree

  // Hashmap values are owned by Node tree
  z3_leakm (&bconf->macros);

  // z3_dropm (&bconf->arguments); // did not free argconf->command
  HashMapIterator it = z3_iterm (&bconf->arguments);
  while (z3_nextm (&it)) {
    ArgumentConfig* argconf = (ArgumentConfig*)it.val;
    Z3_DISCARD_QUAL (free (it.key));
    free ((void*)argconf->command);
    free ((void*)argconf);
  }
  free (bconf->arguments.bfs);

  // All string fields are owned by the Node tree, drop the map
  if (bconf->deps) free (bconf->deps);

  free (bconf);
}

// Free config tree, all values are owned by Node tree
void free_anvil_config (AnvilConfig* conf) {
  if (!conf) return;

  // package, version, author, description are owned by Node tree

  if (conf->workspace) free (conf->workspace);
  if (conf->targets) free_target_config (conf->targets);
  if (conf->build) free_build_config (conf->build);
  free_profile_config (&conf->profiles);

  free (conf);
}
