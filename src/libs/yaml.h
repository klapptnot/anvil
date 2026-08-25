// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot
/// @file yaml.h
/// @brief Minimal YAML parser: lexer tokens, node/value types, and the
///        public parse/free/lookup API.
///
/// Requires C23 (`__STDC_VERSION__ == 202311L`). No dependencies beyond
/// @c notrust.h and @c z3_vector.h.

#pragma once

#ifndef __STDC_VERSION__
#error A modern C standard (like C23) is required
#elif __STDC_VERSION__ != 202311L
#error This code must be compiled with -std=c23
#endif

#include <notrust.h>
#include <z3_vector.h>

/// @def YAML_MAX_NESTING
/// @brief Max object nesting allowed
#define YAML_MAX_NESTING 256

/// @def YAML_MAX_NUM_REPR
/// @brief Max length of a numeric token's value string representation
#define YAML_MAX_NUM_REPR 64

/// @def YAML_INIT_HEAP_SIZE
/// @brief Minimum heap allocation size for small values (keys, booleans).
#define YAML_INIT_HEAP_SIZE 8

/// @def YAML_STACK_BUF_LEN
/// @brief Base allocation size (bytes) for string pools.
#define YAML_STACK_BUF_LEN 512

/// @def YAML_NODE_INIT_CAP
/// @brief Initial child capacity for a YAML container node (map/list).
#define YAML_NODE_INIT_CAP 8

/// @def YAML_CHUNK_SIZE
/// @brief I/O read chunk size, 4 KB (page-aligned).
#define YAML_CHUNK_SIZE (1 << 12)

/// @brief Single characters with specific meaning in YAML syntax tokenizer.
enum YamlChar {
  CHAR_EOF = '\0',           ///< Null character marking end of input.
  CHAR_NEWLINE = '\n',       ///< Line break separating YAML lines.
  CHAR_SPACE = ' ',          ///< Space, used for indentation/formatting.
  CHAR_TAB = '\t',           ///< Tab character (typically invalid in YAML).
  CHAR_COLON = ':',          ///< Separator between keys and values.
  CHAR_DOT = '.',            ///< Decimal point for floating-point numbers.
  CHAR_HASH = '#',           ///< Marks the start of a comment.
  CHAR_QUOTE_SINGLE = '\'',  ///< Delimits single-quoted string literals.
  CHAR_QUOTE_DOUBLE = '"',   ///< Delimits double-quoted strings.
  CHAR_OPEN_BRACKET = '[',   ///< Starts a sequence/array.
  CHAR_CLOSE_BRACKET = ']',  ///< Ends a sequence/array.
  CHAR_OPEN_BRACE = '{',     ///< Starts a mapping/object.
  CHAR_CLOSE_BRACE = '}',    ///< Ends a mapping/object.
  CHAR_COMMA = ',',          ///< Separates elements in collections.
  CHAR_AMPERSAND = '&',      ///< Defines an anchor reference.
  CHAR_ASTERISK = '*',       ///< References an alias.
};

/// @brief Lexical token kinds produced by the tokenizer.
typedef enum {
  TOKEN_UNKNOWN,     ///< Catch-all for unrecognized tokens.
  TOKEN_KEY,         ///< A key in a key-value pair.
  TOKEN_STRING,      ///< Escaped string token.
  TOKEN_STRING_LIT,  ///< Literal (unescaped) string token.
  TOKEN_NUMBER,      ///< Numeric value token.
  TOKEN_BOOLEAN,     ///< Boolean (true/false) token.
  TOKEN_COMMA,       ///< Collection element separator.
  TOKEN_ANCHOR,      ///< Anchor definition token.
  TOKEN_ALIAS,       ///< Alias reference token.
  TOKEN_OPEN_MAP,    ///< Start of a mapping.
  TOKEN_CLOSE_MAP,   ///< End of a mapping.
  TOKEN_OPEN_SEQ,    ///< Start of a sequence.
  TOKEN_CLOSE_SEQ,   ///< End of a sequence.
  TOKEN_EOF,         ///< End of input.
  TOKEN_INDENT,      ///< Container node open (indent increase).
  TOKEN_DEDENT,      ///< Container node close (indent decrease).
} TokenKind;

/// @brief Node types in a parsed YAML document.
typedef enum {
  NODE_MAP,     ///< Key-value mapping.
  NODE_LIST,    ///< Ordered list/array.
  NODE_STRING,  ///< Text string.
  NODE_NUMBER,  ///< Numeric value.
  NODE_BOOLEAN  ///< True/false value.
} NodeKind;

/// @brief Categories of parsing errors that can occur while reading YAML.
typedef enum {
  TAB_INDENTATION,   ///< Incorrect indentation using tabs.
  UNEXPECTED_TOKEN,  ///< Token appears where not expected.
  WRONG_SYNTAX,      ///< General syntax violation.
  KEY_REDEFINITION,  ///< Duplicate key definition.
  UNDEFINED_ALIAS,   ///< Reference to an undefined anchor.
  REDEFINED_ALIAS,   ///< Duplicate anchor definition.
  MISSING_VALUE,     ///< No value provided for a key.
  MISSING_COMMA,     ///< Missing separator in a collection.
  UNCLOSED_QUOTE,    ///< Missing matching quote for a string.
  NUMBER_TOO_LONG,   ///< Number exceeds max integer representation length.
  KEY_TOO_LONG,      ///< Key exceeds maximum allowed length.
} YamlErrorKind;

/// @brief Structured error information for detailed diagnostics.
typedef struct {
  u32 _padding;        ///< Unused; reserved for alignment.
  YamlErrorKind kind;  ///< Type of error encountered.
  nstr exp;            ///< Expected token/context.
  nstr got;            ///< Actual token/context received.
} YamlError;

/// @brief A single node in the parsed YAML tree.
/// The active union member is determined by ::kind.
typedef struct Node Node;

/// @brief A YAML sequence (array) of ::Node pointers.
typedef struct {
  usize size;      ///< Current number of elements.
  usize capacity;  ///< Allocated capacity.
  Node** items;    ///< Array of node pointers.
} YamlList;

/// @brief A single key-value entry within a ::YamlMap.
typedef struct {
  cstr key;   ///< Key string.
  Node* val;  ///< Associated value node.
} YamlMapEntry;

/// @brief A YAML mapping (object) of key-value entries.
typedef struct {
  usize size;             ///< Current number of entries.
  usize capacity;         ///< Allocated capacity.
  YamlMapEntry* entries;  ///< Array of map entries.
} YamlMap;

struct Node {
  NodeKind kind;        ///< Type of node; selects the active union member.
  unsigned int rcount;  ///< Reference count (for `&name` -> `*name` aliasing).
  union {
    cstr string;        ///< Value when kind == NODE_STRING.
    f64 number;         ///< Value when kind == NODE_NUMBER.
    bool boolean;       ///< Value when kind == NODE_BOOLEAN.
    YamlList list;      ///< Value when kind == NODE_LIST.
    YamlMap map;        ///< Value when kind == NODE_MAP.
  };
};

/// @brief Binding between an anchor name (`&name`) and its target node.
typedef struct {
  cstr name;    ///< Anchor name.
  Node* value;  ///< Referenced node.
} YamlAlias;

/// @brief Collection of all anchors defined while parsing a document.
typedef struct {
  YamlAlias* items;  ///< Array of aliases.
  usize length;      ///< Number of aliases.
} YamlAliasList;

/// @brief A single lexical token with metadata pointing to a string pool.
typedef struct {
  TokenKind kind;  ///< Type of token.
  u32 length;      ///< Token length, in bytes, from `raw`.
  cstr raw;        ///< Starting position of the token in the input.
} Token;

/// @brief Backing storage for all string data produced while parsing.
///
/// Owns the string pools and any individually-owned strings so that
/// ::Node values can hold non-owning pointers into it.
typedef struct {
  Vector str_pools;   ///< Pooled string allocations.
  Vector owned_strs;  ///< Individually-owned string allocations.
} YamlStore;

/// @brief Tokenizer/parser state.
///
/// @note Field sizes are intentionally mixed for padding/layout reasons
///       (todo: revisit).
typedef struct {
  i32 iffd;          ///< Input YAML file descriptor.
  u16 blen;          ///< Buffer length.
  u16 cpos;          ///< Current buffer position.
  u16 lpos;          ///< Current position within the line.
  u32 line;          ///< Current line number.
  u16 depth_flw;     ///< Levels of indentation + associated rules.
  ustr chunk;        ///< Current content buffer.
  YamlStore* store;  ///< String storage backing this parse.
  Vector aliases;    ///< Tracked aliases.
  Token cur_token;   ///< Most recently parsed token.
} YamlParser;

/// @brief Parse an entire YAML document from a file.
///
/// @param filepath Path to the YAML file to parse.
/// @param store    String storage to allocate parsed strings into; must
///                 outlive the returned node tree.
/// @return Pointer to the root ::Node of the parsed document. Ownership
///         is transferred to the caller (see ::free_yaml).
[[clang::ownership_returns (yaml_node)]] Node* parse_yaml (
  nstr filepath, YamlStore* store
);

/// @brief Free all resources associated with a parsed YAML node tree.
///
/// @param node Root node previously returned by ::parse_yaml. Consumed
///             by this call; must not be used afterward.
[[clang::ownership_takes (yaml_node, 1)]] void free_yaml (Node* node);

/// @brief Look up a child node in a map by key.
///
/// @param node Node to search; must be of kind ::NODE_MAP.
/// @param key  Key to look up.
/// @return Pointer to the matching value node, or `NULL` if `node` is
///         not a map or `key` is not present.
Node* map_get_node (Node* node, nstr key);
