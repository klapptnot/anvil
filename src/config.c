#include "config.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>
#include <z3_hashmap.h>
#include <z3_vector.h>

void dset_argument_config (ArgumentConfig* acon, Node* node) {
  if (!node || node->kind != NODE_MAP) return;

  Node* vstr = map_get_node (node, "validate_str");
  acon->validate_str = (vstr && vstr->kind == NODE_STRING) ? vstr->string : nullptr;

  Node* cpol = map_get_node (node, "cache_policy");
  acon->cache_policy = (cpol && cpol->kind == NODE_STRING) ? cpol->string : nullptr;

  Node* cmds = map_get_node (node, "commands");
  if (cmds && cmds->kind == NODE_SEQUENCE) {
    acon->commands_count = cmds->sequence.size;
    acon->commands = (const char**)malloc (sizeof (char*) * acon->commands_count);
    for (size_t i = 0; i < acon->commands_count; i++) {
      Node* item = cmds->sequence.items[i];
      acon->commands[i] = (item && item->kind == NODE_STRING) ? item->string : nullptr;
    }
  } else {
    acon->commands = nullptr;
    acon->commands_count = 0;
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

void dset_workspace_config (WorkspaceConfig* wconf, Node* node) {
  if (!node || node->kind != NODE_MAP) return;

  Node* wlibs = map_get_node (node, "libs");
  if (wlibs && wlibs->kind == NODE_STRING) {
    wconf->libs = wlibs->string;
  } else {
    wconf->libs = nullptr;
  }

  Node* wtarget = map_get_node (node, "target");
  if (wtarget && wtarget->kind == NODE_STRING) {
    wconf->target = wtarget->string;
  } else {
    wconf->target = nullptr;
  }
}

void dset_profile_config (Z3HashMap* pconf, Node* node) {
  if (!node || node->kind != NODE_MAP) return;

  for (size_t i = 0; i < node->map.size; ++i) {
    const char* key = node->map.entries[i].key;
    Node* val = node->map.entries[i].val;

    if (!key || !val || val->kind != NODE_SEQUENCE) continue;

    Z3Vector* flags = z3_new_vec (char*);
    for (size_t i = 0; i < val->sequence.size; i++) {
      Node* vi = val->sequence.items[i];
      if (vi && vi->kind == NODE_STRING) {
        z3_push (*flags, vi->string);
      }
    }

    z3_hashmap_put (pconf, key, flags);
  }
}

void dset_target_config (BuildTarget* tconf, Node* node) {
  if (!node || node->kind != NODE_SEQUENCE) return;
  if (node->sequence.size == 0) {
    tconf->count = 0;
    tconf->target = nullptr;
    return;
  }
  tconf->count = node->sequence.size;
  tconf->target = malloc (sizeof (TargetConfig) * node->sequence.size);
  for (size_t i = 0; i < node->sequence.size; i++) {
    TargetConfig* tari = malloc (sizeof (TargetConfig));
    Node* tnode = node->sequence.items[i];
    Node* name = map_get_node (tnode, "name");
    tari->name = (name && name->kind == NODE_STRING) ? name->string : nullptr;

    Node* type = map_get_node (tnode, "type");
    tari->type = (type && type->kind == NODE_STRING) ? type->string : nullptr;

    Node* main = map_get_node (tnode, "main");
    tari->main = (main && main->kind == NODE_STRING) ? main->string : nullptr;

    tnode = map_get_node (tnode, "target");
    if (tnode && tnode->kind == NODE_SEQUENCE) {
      tari->target_count = tnode->sequence.size;
      tari->target = (const char**)malloc (sizeof (char*) * tnode->sequence.size);
      for (size_t j = 0; j < tnode->sequence.size; ++j) {
        Node* elem = tnode->sequence.items[j];
        tari->target[j] = (elem && elem->kind == NODE_STRING) ? elem->string : nullptr;
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
  bconf->compiler = (comp && comp->kind == NODE_STRING) ? comp->string : nullptr;

  Node* std = map_get_node (node, "cstd");
  bconf->cstd = (std && std->kind == NODE_STRING) ? std->string : nullptr;

  // --- macros hashmap ---
  Node* macros = map_get_node (node, "macros");
  if (macros && macros->kind == NODE_MAP) {
    bconf->macros = z3_hashmap_create ();
    for (size_t i = 0; i < macros->map.size; ++i) {
      const char* key = macros->map.entries[i].key;
      Node* val = macros->map.entries[i].val;
      if (key && val && val->kind == NODE_STRING) {
        z3_hashmap_put (bconf->macros, key, val->string);
      }
    }
  }

  // --- arguments hashmap ---
  Node* args = map_get_node (node, "arguments");
  if (args && args->kind == NODE_MAP) {
    bconf->arguments = z3_hashmap_create ();
    for (size_t i = 0; i < args->map.size; ++i) {
      const char* key = args->map.entries[i].key;
      Node* val = args->map.entries[i].val;
      if (key && val && val->kind == NODE_MAP) {
        ArgumentConfig* argconf = malloc (sizeof (ArgumentConfig));
        dset_argument_config (argconf, val);
        z3_hashmap_put (bconf->arguments, key, (void*)argconf);
      }
    }
  }

  // --- deps ---
  Node* deps = map_get_node (node, "deps");
  if (deps && deps->kind == NODE_SEQUENCE) {
    bconf->deps_count = deps->sequence.size;
    bconf->deps = malloc (sizeof (DependencyConfig) * bconf->deps_count);
    for (size_t i = 0; i < bconf->deps_count; ++i) {
      dset_dependency_config (&bconf->deps[i], deps->sequence.items[i]);
    }
  } else {
    bconf->deps = nullptr;
    bconf->deps_count = 0;
  }
}

void dset_anvil_config (AnvilConfig* conf, Node* node) {
  if (!node || node->kind != NODE_MAP) return;

  Node* pkg = map_get_node (node, "package");
  conf->package = (pkg && pkg->kind == NODE_STRING) ? pkg->string : nullptr;

  Node* ver = map_get_node (node, "version");
  conf->version = (ver && ver->kind == NODE_STRING) ? ver->string : nullptr;

  Node* auth = map_get_node (node, "author");
  conf->author = (auth && auth->kind == NODE_STRING) ? auth->string : nullptr;

  Node* desc = map_get_node (node, "description");
  conf->description = (desc && desc->kind == NODE_STRING) ? desc->string : nullptr;

  // --- Workspace ---
  Node* workspc = map_get_node (node, "workspace");
  if (nullptr != workspc && workspc->kind == NODE_MAP) {
    conf->workspace = malloc (sizeof (WorkspaceConfig));
    dset_workspace_config (conf->workspace, workspc);
  } else {
    conf->workspace = nullptr;
  }

  // --- Targets ---
  Node* targets_node = map_get_node (node, "targets");
  if (targets_node && targets_node->kind == NODE_SEQUENCE) {
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
    conf->profiles = z3_hashmap_create ();
    dset_profile_config (conf->profiles, profiles_node);
  } else {
    conf->profiles = nullptr;
  }
}
