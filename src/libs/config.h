// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

#pragma once
#include <notrust.h>
#include <stddef.h>
#include <z3_hashmap.h>

#include "yaml.h"

#define DEFAULT_LIBS_PATH   "#{AWD}/src/libs"
#define DEFAULT_TARGET_PATH "#{AWD}/target"

typedef enum {
  VALIDATE_NONE,
  VALIDATE_STATUS,
  VALIDATE_CONTENT,
  VALIDATE_ALL
} ValidateStr;

typedef enum {
  CACHE_POLICY_NEVER,
  CACHE_POLICY_MEMOIZE,
  CACHE_POLICY_ALWAYS
} CachePolicy;

typedef struct {
  ValidateStr validation;
  CachePolicy cache_policy;
  ustr cached;
  cstr* command;
  usize command_len;
} ArgumentConfig;

typedef struct {
  cstr name;
  cstr type;
  cstr repo;
  cstr path;
} DependencyConfig;

typedef struct {
  cstr compiler;
  cstr cstd;
  HashMap* macros;
  HashMap* arguments;
  DependencyConfig* deps;
  u32 deps_count;
  u32 jobs;
} BuildConfig;

typedef struct {
  cstr libs;
  cstr build;
} WorkspaceConfig;

typedef struct {
  cstr* flags;
  usize flags_count;
} ProfileConfig;

typedef struct {
  cstr name;
  cstr type;
  cstr main;
  cstr* target;
  // HashMap* macros; // TODO
  usize target_count;
} TargetConfig;

typedef struct {
  usize count;
  TargetConfig** target;
} BuildTarget;

typedef struct {
  cstr package;
  cstr version;
  cstr author;
  cstr description;
  WorkspaceConfig* workspace;
  BuildTarget* targets;
  BuildConfig* build;
  HashMap* profiles;
} AnvilConfig;

// Sets up an AnvilConfig structure based on the provided YAML node.
AnvilConfig* dset_anvil_config (Node* node);

// Sets up an ArgumentConfig structure based on the provided YAML node.
void dset_argument_config (ArgumentConfig* acon, Node* node);

// Sets up a DependencyConfig structure based on the provided YAML node.
void dset_dependency_config (DependencyConfig* dcon, Node* node);

// Sets up a WorkspaceConfig structure based on the provided YAML node.
void dset_workspace_config (WorkspaceConfig* wconf, Node* node);

// Sets up a ProfileConfig structure based on the provided YAML node.
void dset_profile_config (HashMap* pconf, Node* node);

// Sets up a TargetConfig structure based on the provided YAML node.
void dset_target_config (BuildTarget* tconf, Node* node);

// Sets up a BuildConfig structure based on the provided YAML node.
void dset_build_config (BuildConfig* bconf, Node* node);

void free_profile_config (HashMap* pconf);
void free_target_config (BuildTarget* tconf);
void free_build_config (BuildConfig* bconf);
void free_anvil_config (AnvilConfig* conf);
