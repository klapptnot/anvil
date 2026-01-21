// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

/**
 * YAML Loader Definitions
 *
 * This module defines the necessary structures and functions to load and parse YAML data.
 * The YAML data is parsed into a tree-like structure of nodes. Each node represents a piece of
 * the YAML document, which can be a map, sequence, string, number, or boolean. The core parsing
 * mechanism handles tokenization, error reporting, and recursive tree-building to represent
 * complex YAML documents.
 *
 * Key components:
 * - `YamlChar`: Enum of special characters used in YAML syntax.
 * - `TokenKind`: Enum for the types of tokens recognized in YAML documents.
 * - `YamlErrorKind`: Enum for identifying common parsing errors.
 * - `NodeKind`: Enum defining the possible types of YAML nodes.
 * - `Node`: A recursive structure representing the nodes in the parsed YAML tree.
 *
 * Functions provide functionality for:
 * - Tokenizing YAML input.
 * - Parsing YAML maps, sequences, strings, numbers, and booleans.
 * - Error handling with detailed messages for common YAML issues.
 *
 * The end result is a tree of nodes that represents the structured YAML data.
 */
#pragma once

#include <stdint.h>

#include "z3_string.h"
#include "z3_vector.h"
#ifndef __STDC_VERSION__
#error A modern C standard (like C23) is required
#elif __STDC_VERSION__ != 202311L
#error This code must be compiled with -std=c23
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define STACK_VALUE_BUFFER_SIZE 256        // max stack value size before heap
#define HEAP_VALUE_MIN_SIZE     8          // small values: keys, booleans
#define MAX_ERROR_LINE_LENGTH   512        // terminal-friendly line length
#define NODE_INITIAL_CAPACITY   8          // typical YAML node child count
#define YAML_CHUNK_SIZE         (1 << 12)  // 4 KB (page-aligned I/O)

// Single characters with specific meanings in YAML syntax
enum YamlChar {
  CHAR_EOF = '\0',           // Null character marking end of input
  CHAR_NEWLINE = '\n',       // Line break for separating YAML lines
  CHAR_SPACE = ' ',          // Space for readability and formatting
  CHAR_TAB = '\t',           // Tab character (typically invalid in YAML)
  CHAR_COLON = ':',          // Separator between keys and values
  CHAR_DOT = '.',            // Decimal point for floating-point numbers
  CHAR_HASH = '#',           // Marks the start of a comment
  CHAR_QUOTE_SINGLE = '\'',  // Delimits single-quoted string literals
  CHAR_QUOTE_DOUBLE = '"',   // Delimits double-quoted strings
  CHAR_OPEN_BRACKET = '[',   // Starts a sequence/array
  CHAR_CLOSE_BRACKET = ']',  // Ends a sequence/array
  CHAR_OPEN_BRACE = '{',     // Starts a mapping/object
  CHAR_CLOSE_BRACE = '}',    // Ends a mapping/object
  CHAR_COMMA = ',',          // Separates elements in collections
  CHAR_AMPERSAND = '&',      // Defines an anchor reference
  CHAR_ASTERISK = '*',       // References an alias
};

// Enumeration of token types for lexical analysis
// Helps identify the context and type of parsed tokens
typedef enum {
  TOKEN_UNKNOWN,     // Catch-all for unrecognized tokens
  TOKEN_KEY,         // Identifies a key in key-value pairs
  TOKEN_STRING,      // Escaped string token
  TOKEN_STRING_LIT,  // Literal string token
  TOKEN_NUMBER,      // Numeric value token
  TOKEN_BOOLEAN,     // Boolean (true/false) token
  TOKEN_COMMA,       // Collection element separator
  TOKEN_ANCHOR,      // Anchor definition token
  TOKEN_ALIAS,       // Alias reference token
  TOKEN_OPEN_MAP,    // Start of mapping token
  TOKEN_CLOSE_MAP,   // End of mapping token
  TOKEN_OPEN_SEQ,    // Start of sequence token
  TOKEN_CLOSE_SEQ,   // End of sequence token
  TOKEN_EOF,         // End of input token
  TOKEN_INDENT,      //= Container node open
  TOKEN_DEDENT,      //= Container node close
} TokenKind;

// Enumeration of possible node types in parsed YAML
// Represents different data structures and primitive types
typedef enum {
  NODE_MAP,       // Key-value mapping
  NODE_SEQUENCE,  // Ordered list/array
  NODE_STRING,    // Text string
  NODE_NUMBER,    // Numeric value
  NODE_BOOLEAN    // True/false value
} NodeKind;

// Enumeration of potential parsing errors
// Helps identify specific issues during YAML parsing
typedef enum {
  TAB_INDENTATION,   // Incorrect indentation using tabs
  UNEXPECTED_TOKEN,  // Token appears where not expected
  WRONG_SYNTAX,      // General syntax violation
  KEY_REDEFINITION,  // Duplicate key definition
  UNDEFINED_ALIAS,   // Reference to undefined anchor
  REDEFINED_ALIAS,   // Duplicate anchor definition
  MISSING_VALUE,     // No value provided for a key
  MISSING_COMMA,     // Missing separator in collections
  UNCLOSED_QUOTE,    // Missing matching quote for strings
} YamlErrorKind;

// Structured error information for detailed error reporting
typedef struct {
  uint32_t _padding;   // hmm
  YamlErrorKind kind;  // Type of error encountered
  const char* exp;     // Expected token/context
  const char* got;     // Actual token/context received
} YamlError;

// Recursive node structure for representing YAML data
typedef struct Node Node;

// Sequence (list/array) representation
typedef struct {
  size_t size;      // Current number of elements
  size_t capacity;  // Allocated capacity
  Node** items;     // Array of node pointers
} Sequence;

// Map entry with key-value pair
typedef struct {
  char* key;  // Key string
  Node* val;  // Associated value node
} MapEntry;

// Map (object) representation
typedef struct {
  size_t size;        // Current number of entries
  size_t capacity;    // Allocated capacity
  MapEntry* entries;  // Array of map entries
} Map;

struct Node {
  NodeKind kind;        // Type of node
  unsigned int rcount;  // Reference count (for &name -> *name)
  union {
    char* string;       // String node value
    double number;      // Numeric node value
    bool boolean;       // Boolean node value
    Sequence sequence;  // Sequence node value
    Map map;            // Map node value
  };
};

// Alias/anchor representation
typedef struct {
  char* name;   // Anchor name
  Node* value;  // Referenced node
} YamlAlias;

// List of defined aliases
typedef struct {
  YamlAlias* items;  // Array of aliases
  size_t length;     // Number of aliases
} YamlAliasList;

// Token representation with detailed metadata
typedef struct {
  TokenKind kind;   // Type of token
  uint32_t length;  // Token length
  char* raw;        // Starting position in input
} Token;

// Tokenizer state tracking for parsing (weird sized for padding sense, todo)
typedef struct {
  int iffd;            // Input YAML file descriptor
  uint16_t blen;       // Buffer len
  uint16_t lred;       // Last buf read
  char* chunk;         // Current content buffer
  uint16_t cpos;       // Current buffer position
  uint16_t lpos;       // Current pos in line
  uint16_t line;       // Current line number
  uint16_t root_mark;  // Levels of indentation + rules
  String* strline;     // All strings and keys
  Vector aliases;      // Tracked aliases
  Token cur_token;     // Most recently parsed token
} YamlParser;

// Top-level function to parse an entire YAML input string
Node* parse_yaml (const char* filepath, String* strs)
  __attribute__ ((ownership_holds (malloc, 1)));

// Free all resources associated with a parsed YAML node
void free_yaml (Node* node);

// Retrieve an arbitrary node from a map by its key
Node* map_get_node (Node* node, const char* key);
