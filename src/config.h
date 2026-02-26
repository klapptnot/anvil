// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

#pragma once
#include <notrust.h>
#include <stddef.h>
#include <z3_hashmap.h>

#include "yaml.h"

#define DEFAULT_LIBS_PATH   "#{AWD}/src/libs"
#define DEFAULT_TARGET_PATH "#{AWD}/target"

typedef struct {
  cstr validation;
  cstr cache_policy;
  const u8** command;
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
  usize jobs;
  HashMap* macros;
  HashMap* arguments;
  DependencyConfig* deps;
  usize deps_count;
} BuildConfig;

typedef struct {
  cstr libs;
  cstr build;
} WorkspaceConfig;

typedef struct {
  const u8** flags;
  usize flags_count;
} ProfileConfig;

typedef struct {
  cstr name;
  cstr type;
  cstr main;
  const u8** target;
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

// Sets up an AnvilConfig structure based on the provided YAML node.
void dset_anvil_config (AnvilConfig* conf, Node* node);

void free_profile_config (HashMap* pconf);
void free_target_config (BuildTarget* tconf);
void free_build_config (BuildConfig* bconf);
void free_anvil_config (AnvilConfig* conf);
