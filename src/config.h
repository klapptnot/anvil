#pragma once
#include <stddef.h>
#include <z3_hashmap.h>

#include "yaml.h"

#define DEFAULT_LIBS_PATH   "#{AWD}/src/libs"
#define DEFAULT_TARGET_PATH "#{AWD}/target"

typedef struct {
  const char* validate_str;
  const char* cache_policy;
  const char** commands;
  size_t commands_count;
} ArgumentConfig;

typedef struct {
  const char* name;
  const char* type;
  const char* repo;
  const char* path;
} DependencyConfig;

typedef struct {
  const char* compiler;
  const char* cstd;
  Z3HashMap* macros;
  Z3HashMap* arguments;
  DependencyConfig* deps;
  size_t deps_count;
} BuildConfig;

typedef struct {
  const char* libs;
  const char* target;
} WorkspaceConfig;

typedef struct {
  const char** flags;
  size_t flags_count;
} ProfileConfig;

typedef struct {
  const char* name;
  const char* type;
  const char* main;
  const char** target;
  size_t target_count;
} TargetConfig;

typedef struct {
  size_t count;
  TargetConfig** target;
} BuildTarget;

typedef struct {
  const char* package;
  const char* version;
  const char* author;
  const char* description;
  WorkspaceConfig* workspace;
  BuildTarget* targets;
  BuildConfig* build;
  Z3HashMap* profiles;
} AnvilConfig;

// Sets up an ArgumentConfig structure based on the provided YAML node.
void dset_argument_config (ArgumentConfig* acon, Node* node);

// Sets up a DependencyConfig structure based on the provided YAML node.
void dset_dependency_config (DependencyConfig* dcon, Node* node);

// Sets up a WorkspaceConfig structure based on the provided YAML node.
void dset_workspace_config (WorkspaceConfig* wconf, Node* node);

// Sets up a ProfileConfig structure based on the provided YAML node.
void dset_profile_config (Z3HashMap* pconf, Node* node);

// Sets up a TargetConfig structure based on the provided YAML node.
void dset_target_config (BuildTarget* tconf, Node* node);

// Sets up a BuildConfig structure based on the provided YAML node.
void dset_build_config (BuildConfig* bconf, Node* node);

// Sets up an AnvilConfig structure based on the provided YAML node.
void dset_anvil_config (AnvilConfig* conf, Node* node);
