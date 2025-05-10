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

#ifndef __STDC_VERSION__
#error A modern C standard (like C23) is required
#elif __STDC_VERSION__ != 202311L
#error This code must be compiled with -std=c23
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

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

// Tags for token identification
enum TagKind {
  TAG_NULL,
  TAG_ANCHOR,
  TAG_ALIAS,
  TAG_KEY,
  TAG_BOOL,
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
  TOKEN_COLON,       // Separator token
  TOKEN_COMMA,       // Collection element separator
  TOKEN_NEWLINE,     // Line break token
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
  YamlErrorKind kind;  // Type of error encountered
  const char* exp;     // Expected token/context
  const char* got;     // Actual token/context received
  size_t pos;          // Error position in input
  size_t len;          // Length of problematic section
} YamlError;

// Enumeration of possible node types in parsed YAML
// Represents different data structures and primitive types
typedef enum {
  NODE_MAP,       // Key-value mapping
  NODE_SEQUENCE,  // Ordered list/array
  NODE_STRING,    // Text string
  NODE_NUMBER,    // Numeric value
  NODE_BOOLEAN    // True/false value
} NodeKind;

// Token representation with detailed metadata
typedef struct {
  TokenKind kind;  // Type of token
  size_t start;    // Starting position in input
  size_t length;   // Token length
  size_t line;     // Line number
  size_t column;   // Column position
} Token;

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
  size_t rcount;        // Reference count (Alias reference count)
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

// Tokenizer state tracking for parsing
typedef struct {
  const char* input;      // Input YAML string
  size_t cpos;            // Current parsing position
  size_t line;            // Current line number
  size_t ccol;            // Current column
  YamlAliasList aliases;  // Tracked aliases
  Token cur_token;        // Most recently parsed token
} Tokenizer;

typedef struct {
  int current_node;  // Currently processed node type
  Tokenizer t;
} YamlParser;

// Create a token with specified metadata
Token create_token (TokenKind kind, size_t start, size_t length, size_t line, size_t column);

// Terminate parsing with a detailed error message
void parser_error (Tokenizer* tokenizer, YamlError error) __attribute__ ((__noreturn__));

// Return current token without consuming it
Token peek_token (Tokenizer* tokenizer);

// Return the next token, directly from input
Token next_token (Tokenizer* tokenizer);

// Skip over whitespace characters in the input
void skip_whitespace (Tokenizer* tokenizer);

// Read and return the next character from the input
void read_char (Tokenizer* tokenizer);

// Parse a YAML mapping (key-value dictionary)
Node* parse_map (Tokenizer* tokenizer);

// Parse a YAML sequence (array/list)
Node* parse_seq (Tokenizer* tokenizer);

// Parse any valid YAML value (string, number, map, sequence, etc.)
Node* parse_value (Tokenizer* tokenizer);

// Extract the string value of a token from the input
char* token_value (Tokenizer* tokenizer, Token token) __attribute__ ((malloc));

// Parse an alias reference to a previously defined anchor
Node* parse_alias (Tokenizer* tokenizer, Token token);

// Parse a string token (both quoted and unquoted)
Node* parse_string (Tokenizer* tokenizer, Token token);

// Parse a numeric value token
Node* parse_number (Tokenizer* tokenizer, Token token);

// Parse a boolean value token
Node* parse_boolean (Tokenizer* tokenizer, Token token);

// Create a new node with a specified type
Node* create_node (NodeKind kind);

// Recursively free memory allocated for a node
void free_node (Node* node);

// Add a key-value pair to a map
void map_add (Map* map, char* key, Node* value);

// Add an item to a sequence
void sequence_add (Sequence* seq, Node* item);

// Top-level function to parse an entire YAML input string
Node* parse_yaml (const char* input) __attribute__ ((ownership_holds (malloc, 1)));

// Free all resources associated with a parsed YAML node
void free_yaml (Node* node);

// Retrieve an arbitrary node from a map by its key
Node* map_get_node (Node* node, const char* key);

#define CT_TOKEN_KIND_STRING(kind) #kind

#ifdef __YAML_TEST

inline const char* node_kind_to_string (NodeKind kind) {
  switch (kind) {
    case NODE_MAP:
      return "NODE_MAP";
    case NODE_SEQUENCE:
      return "NODE_SEQUENCE";
    case NODE_STRING:
      return "NODE_STRING";
    case NODE_NUMBER:
      return "NODE_NUMBER";
    case NODE_BOOLEAN:
      return "NODE_BOOLEAN";
    default:
      return "UNKNOWN";
  }
}

#define token_dbg(t, token)                                                                  \
  {                                                                                          \
    char* v = token_value (t, token);                                                        \
    printf ("TOKEN: ~%3zu %36s '%s'\n", token.length, token_kind_to_string (token.kind), v); \
    free (v);                                                                                \
  }

[[clang::always_inline]] const char* nodeKindToValue (Node* node) {
  static char _buf[20];

  switch (node->kind) {
    case NODE_STRING:
      return node->string;
    case NODE_NUMBER:
      snprintf (_buf, sizeof (_buf), "%f", node->number);
      return _buf;
    case NODE_BOOLEAN:
      return node->boolean ? "true" : "false";
    default:
      return "UNKNOWN";
  }
}

void map_walk (Node* node, int indent);
void seq_walk (Node* node, int indent) {
  for (size_t i = 0; i < node->sequence.size; i++) {
    Node* value = node->sequence.items[i];
    if (value->kind == NODE_MAP) {
      printf (
          "\x1b[1;36m%*s[%zu]{%zu}%s:\x1b[0m\n", indent, "", i, value->map.size,
          node_kind_to_string (value->kind)
      );
      map_walk (value, indent + 2);
      continue;
    }
    if (value->kind == NODE_SEQUENCE) {
      printf (
          "%*s\x1b[1;34m[%zu]{%zu}%s:\x1b[0m\n", indent, "", i, value->map.size,
          node_kind_to_string (value->kind)
      );
      seq_walk (value, indent + 2);
      continue;
    }
    printf (
        "%*s\x1b[1;32m[%zu]%s:\x1b[0m %s\n", indent, "", i, node_kind_to_string (value->kind),
        nodeKindToValue (value)
    );
  }
}

void map_walk (Node* node, int indent) {
  for (size_t i = 0; i < node->map.size; i++) {
    char* name = node->map.entries[i].key;
    Node* value = node->map.entries[i].val;
    if (value->kind == NODE_MAP) {
      printf (
          "\x1b[1;36m%*s[%zu]{%zu}%s:\x1b[0m %s\n", indent, "", i, value->map.size,
          node_kind_to_string (value->kind), name
      );
      map_walk (value, indent + 2);
      continue;
    }
    if (value->kind == NODE_SEQUENCE) {
      printf (
          "%*s\x1b[1;34m[%zu]{%zu}%s:\x1b[0m %s\n", indent, "", i, value->map.size,
          node_kind_to_string (value->kind), name
      );
      seq_walk (value, indent + 2);
      continue;
    }
    printf (
        "%*s\x1b[1;32m[%zu]%s:\x1b[0m %s = %s\n", indent, "", i,
        node_kind_to_string (value->kind), name, nodeKindToValue (value)
    );
  }
}

#endif  // __YAML_DISPLAY
