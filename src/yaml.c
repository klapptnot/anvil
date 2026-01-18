// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#ifdef _YAML_TEST
#define Z3_TOYS_IMPL
#define Z3_STRING_IMPL
#endif

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yaml.h>
#include <z3_string.h>
#include <z3_toys.h>
#include <z3_vector.h>

#include "paerr.c"  // NOLINT (bugprone-suspicious-include)

static Node* parse_value (YamlParser* yp);

static int fd_open_file (const char* filepath) {
  if (!filepath) return -1;

  int fd = open (filepath, O_RDONLY | O_NONBLOCK);
  if (fd == -1) return -1;

  struct stat st;
  if (fstat (fd, &st) == -1 || !S_ISREG (st.st_mode)) {
    close (fd);
    return -1;
  }

  return fd;
}

static ssize_t refill_buffers (YamlParser* yp) {
  ssize_t n = read (yp->ifd, yp->buff, YAML_CHUNK_SIZE);
  if (n < 0) die ("could not continue reading file: %s", strerror (errno));

  yp->cpos = 0;
  yp->blen = (uint16_t)n;
  yp->bred = (uint16_t)n;
  return n;
}

extern const char* const token_kind_strings[];
const char* node_kind_names[];

extern const uint8_t char_flags[256];
#define token_kind_to_string(kind) token_kind_strings[kind]
#define is_number_parseable(c)     (isdigit (c) || (c) == '.' || (c) == '-' || (c) == '+')
#define is_valid_anchor(c)         (isalnum (c) || (c) == '_' || (c) == '-')

// Peek at current character without advancing the position
[[clang::always_inline]]
static char peek_char (YamlParser* yp) {
  return yp->buff[yp->cpos];
}

[[clang::always_inline]]
static Token peek_token (YamlParser* yp) {
  return yp->cur_token;
}

[[clang::always_inline]]
static bool eof_reached (YamlParser* yp) {
  return (yp->bred == 0 && yp->cpos >= yp->blen) != 0;
}

[[clang::always_inline]]
static void skip_char (YamlParser* yp) {
  if (peek_char (yp) == CHAR_NEWLINE) {
    yp->line++;
    yp->lpos = 0;
  } else {
    yp->lpos++;
  }

  // buffer is empty (read did not refill cuz EOF), and still, not handled
  // not dying is just making it loop because caller did not handle, and won't
  if (yp->blen == 0 && yp->bred == 0) die ("Check who called me I should not get to skip if EOF :anger:");
  if (++yp->cpos >= yp->blen) refill_buffers (yp);
}

[[clang::always_inline]]
static void skip_comment (YamlParser* yp) {
  while (!eof_reached (yp) && peek_char (yp) != CHAR_NEWLINE) {
    skip_char (yp);
  }
}

// skip and count whitespace (excluding newlines)
static void skip_whitespace (YamlParser* yp) {
  while (peek_char (yp) == CHAR_SPACE) {
    skip_char (yp);
    if (eof_reached (yp)) return;
  }

  if (peek_char (yp) == CHAR_TAB) {
    parser_error (
      yp,
      (YamlError) {
        .kind = TAB_INDENTATION,
        .got = "",
        .exp = "",
      }
    );
  }
}

static char skip_all_whitespace (YamlParser* yp) {
  while (!eof_reached (yp)) {
    skip_whitespace (yp);

    char c = peek_char (yp);
    if (c != CHAR_NEWLINE) return c;

    // it IS a newline, do-while
    yp->lpos = 0;
    do {
      if (++yp->cpos >= YAML_CHUNK_SIZE) refill_buffers (yp);
      yp->line++;
    } while (peek_char (yp) == CHAR_NEWLINE);
  }
  return CHAR_EOF;
}

#define create_token(k, v, l)              \
  (Token) {                                \
    .kind = (k), .raw = (v), .length = (l) \
  }

#define create_token_lit(k, v)                                        \
  (Token) {                                                           \
    .kind = (k), .raw = (char*)strdup (v), .length = (sizeof (v) - 1) \
  }

static Token try_string_unesc (YamlParser* yp) {
  skip_char (yp);  // skip opening quote

  String s = z3_str (HEAP_VALUE_MIN_SIZE);
  char ilubsm[STACK_VALUE_BUFFER_SIZE];
  uint16_t len = 0;
  while (!eof_reached (yp) && peek_char (yp) != CHAR_QUOTE_DOUBLE &&
         peek_char (yp) != CHAR_NEWLINE) {
    if (len >= STACK_VALUE_BUFFER_SIZE) {
      z3_pushl (&s, ilubsm, len);
      len = 0;
    }
    ilubsm[len++] = peek_char (yp);
    skip_char (yp);
  }

  if (peek_char (yp) != CHAR_QUOTE_DOUBLE) {
    char c = peek_char (yp);
    parser_error (
      yp,
      (YamlError) {.kind = UNCLOSED_QUOTE, .got = !eof_reached (yp) ? &c : "EOF", .exp = "\""}
    );
  }
  if (len > 0) z3_pushl (&s, ilubsm, len);

  // String struct is stack allocated, but the `chr` is heap
  // so, you can use the pointer as if just malloc'ed
  // the struct ceases to exist
  String unesc = z3_unescape (s.chr, s.len);

  skip_char (yp);  // skip closing quote
  return (yp->cur_token = create_token (TOKEN_STRING, unesc.chr, (uint32_t)unesc.len));
}

static Token try_string_lit (YamlParser* yp) {
  String s = z3_str (HEAP_VALUE_MIN_SIZE);
  char ilubsm[STACK_VALUE_BUFFER_SIZE];
  uint16_t len = 0;
  while (peek_char (yp) == CHAR_QUOTE_SINGLE) {
    skip_char (yp);  // open quote, or the second one of ''

    // skip until boundary
    while (!eof_reached (yp) && peek_char (yp) != CHAR_QUOTE_SINGLE &&
           peek_char (yp) != CHAR_NEWLINE) {
      if (len >= STACK_VALUE_BUFFER_SIZE) {
        z3_pushl (&s, ilubsm, len);
        len = 0;
      }
      ilubsm[len++] = peek_char (yp);
      skip_char (yp);
    }

    if (peek_char (yp) != CHAR_QUOTE_SINGLE) {
      char c = peek_char (yp);
      parser_error (
        yp,
        (YamlError) {// ugh :anger:
                     .kind = UNCLOSED_QUOTE,
                     .got = !eof_reached (yp) ? &c : "EOF",
                     .exp = &c
        }
      );
    }

    // either an '' or definite end
    skip_char (yp);  // skip one

    // if '', then continue after it
    if (peek_char (yp) != CHAR_QUOTE_SINGLE) break;
    if (len >= STACK_VALUE_BUFFER_SIZE) {
      z3_pushl (&s, ilubsm, len);
      len = 0;
    }
    ilubsm[len++] = peek_char (yp);
  }
  if (len > 0) z3_pushl (&s, ilubsm, len);

  return (yp->cur_token = create_token (TOKEN_STRING_LIT, s.chr, (uint32_t)s.len));
}

static Token try_anchor_or_alias (YamlParser* yp, enum TagKind ident_flag) {
  String s = z3_str (HEAP_VALUE_MIN_SIZE);
  char ilubsm[STACK_VALUE_BUFFER_SIZE];
  uint16_t len = 0;

  while (!eof_reached (yp) && is_valid_anchor (peek_char (yp))) {
    if (len >= STACK_VALUE_BUFFER_SIZE) {
      z3_pushl (&s, ilubsm, len);
      len = 0;
    }
    ilubsm[len++] = peek_char (yp);
    skip_char (yp);
  }

  if (len > 0) z3_pushl (&s, ilubsm, len);

  TokenKind kind = (ident_flag == TAG_ANCHOR) ? TOKEN_ANCHOR : TOKEN_ALIAS;
  return (yp->cur_token = create_token (kind, s.chr, (uint32_t)s.len));
}

static Token try_token_number (YamlParser* yp) {
  String s = z3_str (HEAP_VALUE_MIN_SIZE);
  char ilubsm[STACK_VALUE_BUFFER_SIZE];
  uint16_t len = 0;

  while (!eof_reached (yp) && is_number_parseable (peek_char (yp))) {
    if (len >= STACK_VALUE_BUFFER_SIZE) {
      z3_pushl (&s, ilubsm, len);
      len = 0;
    }
    ilubsm[len++] = peek_char (yp);
    skip_char (yp);
  }

  if (!eof_reached (yp) && peek_char (yp) != CHAR_SPACE) {
    // definitively a number
    if (len > 0) z3_pushl (&s, ilubsm, len);
    return create_token (TOKEN_NUMBER, s.chr, (uint32_t)s.len);
  }

  // Not a number, caller should try other parsers
  z3_drops (&s);
  return (Token) {.kind = TOKEN_UNKNOWN, .raw = nullptr, .length = 0};
}

static Token try_boolean (YamlParser* yp, char first_char) {
  if (first_char == 't') {
    // Try to match "true"
    if (!eof_reached (yp) && peek_char (yp) == 't') {
      skip_char (yp);
      if (!eof_reached (yp) && peek_char (yp) == 'r') {
        skip_char (yp);
        if (!eof_reached (yp) && peek_char (yp) == 'u') {
          skip_char (yp);
          if (!eof_reached (yp) && peek_char (yp) == 'e') {
            skip_char (yp);
            if ((int)eof_reached (yp) || peek_char (yp) == CHAR_SPACE ||
                peek_char (yp) == CHAR_NEWLINE) {
              return create_token_lit (TOKEN_BOOLEAN, "true");
            }
          }
        }
      }
    }
  } else if (first_char == 'f') {
    // Try to match "false"
    if (!eof_reached (yp) && peek_char (yp) == 'f') {
      skip_char (yp);
      if (!eof_reached (yp) && peek_char (yp) == 'a') {
        skip_char (yp);
        if (!eof_reached (yp) && peek_char (yp) == 'l') {
          skip_char (yp);
          if (!eof_reached (yp) && peek_char (yp) == 's') {
            skip_char (yp);
            if (!eof_reached (yp) && peek_char (yp) == 'e') {
              skip_char (yp);
              if ((int)eof_reached (yp) || peek_char (yp) == CHAR_SPACE ||
                  peek_char (yp) == CHAR_NEWLINE) {
                return create_token_lit (TOKEN_BOOLEAN, "false");
              }
            }
          }
        }
      }
    }
  }

  // Not a boolean, caller should try other parsers
  return (Token) {.kind = TOKEN_UNKNOWN, .raw = nullptr, .length = 0};
}

static Token try_key (YamlParser* yp) {
  String s = z3_str (HEAP_VALUE_MIN_SIZE);
  char ilubsm[STACK_VALUE_BUFFER_SIZE];
  uint16_t len = 0;

  // First character already peeked by caller, collect it
  if (len >= STACK_VALUE_BUFFER_SIZE) {
    z3_pushl (&s, ilubsm, len);
    len = 0;
  }
  ilubsm[len++] = peek_char (yp);
  skip_char (yp);

  // Parse as key
  while (!eof_reached (yp)) {
    if (peek_char (yp) == CHAR_COLON) {
      skip_char (yp);
      if ((int)eof_reached (yp) || peek_char (yp) == CHAR_SPACE) break;

      // Colon wasn't followed by space/EOF, include it in key
      if (len >= STACK_VALUE_BUFFER_SIZE) {
        z3_pushl (&s, ilubsm, len);
        len = 0;
      }
      ilubsm[len++] = CHAR_COLON;
      continue;
    }

    if (len >= STACK_VALUE_BUFFER_SIZE) {
      z3_pushl (&s, ilubsm, len);
      len = 0;
    }
    ilubsm[len++] = peek_char (yp);
    skip_char (yp);
  }

  if (len > 0) z3_pushl (&s, ilubsm, len);

  return (yp->cur_token = create_token (TOKEN_KEY, s.chr, (uint32_t)s.len));
}
static Token next_token (YamlParser* yp) {
  enum TagKind ident_flag = TAG_NULL;
go_back_to_start:
  if (eof_reached (yp)) {
    yp->cur_token = (Token) {.kind = TOKEN_EOF, .raw = nullptr, .length = 0};
    return yp->cur_token;
  }

  skip_whitespace (yp);
  char c = peek_char (yp);

  switch (c) {
    case CHAR_NEWLINE:
      skip_all_whitespace (yp);
      goto go_back_to_start;

    case CHAR_HASH:
      skip_comment (yp);
      goto go_back_to_start;

    case CHAR_AMPERSAND:
      ident_flag = TAG_ANCHOR;
      skip_char (yp);
      goto go_back_to_start;

    case CHAR_ASTERISK:
      ident_flag = TAG_ALIAS;
      skip_char (yp);
      goto go_back_to_start;

    case CHAR_COMMA:
    case CHAR_OPEN_BRACE:
    case CHAR_OPEN_BRACKET:
    case CHAR_CLOSE_BRACE:
    case CHAR_CLOSE_BRACKET: {
      TokenKind type;
      const char* lit;
      switch (c) { /* NOLINT (bugprone-switch-missing-default-case) literally never */
        case CHAR_COMMA:
          type = TOKEN_COMMA;
          lit = ",";
          break;
        case CHAR_OPEN_BRACE:
          type = TOKEN_OPEN_MAP;
          lit = "{";
          break;
        case CHAR_OPEN_BRACKET:
          type = TOKEN_OPEN_SEQ;
          lit = "[";
          break;
        case CHAR_CLOSE_BRACE:
          type = TOKEN_CLOSE_MAP;
          lit = "}";
          break;
        case CHAR_CLOSE_BRACKET:
          type = TOKEN_CLOSE_SEQ;
          lit = "]";
          break;
      }
      yp->cur_token = create_token_lit (type, lit);
      skip_char (yp);
      return yp->cur_token;
    }

    case CHAR_QUOTE_DOUBLE:
      return try_string_unesc (yp);

    case CHAR_QUOTE_SINGLE:
      return try_string_lit (yp);

    default:
      if (ident_flag != TAG_NULL) {
        return try_anchor_or_alias (yp, ident_flag);
      }

      if (is_number_parseable (c)) {
        Token num_token = try_token_number (yp);
        if (num_token.kind != TOKEN_UNKNOWN) return num_token;
        // Fall through to boolean/key parsing if not a number
      }

      if (c == 't' || c == 'f') {
        Token bool_token = try_boolean (yp, c);
        if (bool_token.kind != TOKEN_UNKNOWN) return bool_token;
        // Fall through to key parsing if not a boolean
      }

      return try_key (yp);
  }
}

static Node* create_node (NodeKind kind) {
  Node* node = (Node*)malloc (sizeof (Node));

  if (!node) die ("Out of memory allocating %zu bytes", sizeof (Node));

  node->kind = kind;
  node->rcount = 1;

  switch (kind) {
    case NODE_MAP:
      node->map.size = 0;
      node->map.capacity = NODE_INITIAL_CAPACITY;
      node->map.entries = (MapEntry*)malloc (sizeof (MapEntry) * node->map.capacity);
      if (!node->map.entries) {
        free (node);
        die ("Out of memory allocating %zu bytes", sizeof (MapEntry) * node->map.capacity);
      }
      break;

    case NODE_SEQUENCE:
      node->sequence.size = 0;
      node->sequence.capacity = NODE_INITIAL_CAPACITY;
      node->sequence.items = (Node**)malloc (sizeof (Node*) * node->sequence.capacity);
      if (!node->sequence.items) {
        free (node);
        die ("Out of memory allocating %zu bytes", sizeof (Node*) * node->sequence.capacity);
      }
      break;

    case NODE_STRING:
    case NODE_NUMBER:
    case NODE_BOOLEAN:
      // these will be initialized when value is known
      break;
  }

  return node;
}

void free_node (Node* node) {
  if (!node) return;
  if (node->rcount > 1) {
    node->rcount--;
    return;
  }

  switch (node->kind) {
    case NODE_STRING:
      free ((void*)node->string);
      node->string = nullptr;
      break;

    case NODE_MAP:
      for (size_t i = 0; i < node->map.size; i++) {
        free_node (node->map.entries[i].val);
        free (node->map.entries[i].key);
      }
      free (node->map.entries);
      break;

    case NODE_SEQUENCE:
      for (size_t i = 0; i < node->sequence.size; i++) {
        free_node (node->sequence.items[i]);
      }
      free ((void*)node->sequence.items);
      break;

    // No dynamic memory to free
    case NODE_NUMBER:
    case NODE_BOOLEAN:
      break;
  }

  free (node);
  node = nullptr;
}

void map_add (Map* map, char* key, Node* val) {
  if (map->size >= map->capacity) {
    map->capacity *= 2;
    MapEntry* new_entries =
      (MapEntry*)realloc (map->entries, sizeof (MapEntry) * map->capacity);

    if (!new_entries)
      die ("Out of memory allocating %zu bytes", sizeof (MapEntry) * map->capacity);

    map->entries = new_entries;
  }

  map->entries[map->size].key = key;
  map->entries[map->size].val = val;
  map->size++;
}

void sequence_add (Sequence* seq, Node* item) {
  if (seq->size >= seq->capacity) {
    seq->capacity *= 2;
    Node** new_items = (Node**)realloc ((void*)seq->items, sizeof (Node*) * seq->capacity);

    if (!new_items) die ("Out of memory allocating %zu bytes", sizeof (Node*) * seq->capacity);

    seq->items = new_items;
  }

  seq->items[seq->size] = item;
  seq->size++;
}

static Node* parse_alias (YamlParser* yp, Token token) {
  size_t aliases = yp->aliases.length;
  if (aliases == 0) return nullptr;

  const char* alias_name = token.raw;
  for (size_t i = 0; i < aliases; i++) {
    if (strcmp (yp->aliases.items[i].name + 1, alias_name) == 0) {
      return yp->aliases.items[i].value;
    }
  }
  return nullptr;
}

static Node* parse_number (Token token) {
  Node* node = create_node (NODE_NUMBER);

  char* ver_value = malloc (token.length);

  if (ver_value == NULL) die ("Out of memory allocating %d bytes", token.length);

  const char* value = token.raw;
  size_t i = 0;
  size_t l = 0;
  while (i++ <= token.length) {
    // Allowing `1_000_000` to be `1000000`
    if (value[i] == '_') continue;
    ver_value[l++] = value[i];
  }

  node->number = strtod (value, nullptr);
  free (ver_value);
  return node;
}

static Node* parse_seq (YamlParser* yp) {
  Node* node = create_node (NODE_SEQUENCE);

  while (true) {
    if (peek_char (yp) == CHAR_CLOSE_BRACKET) {
      next_token (yp);
      break;
    };

    Node* item = parse_value (yp);
    Token token = next_token (yp);

    if (token.kind == TOKEN_EOF)
      parser_error (
        yp,
        (YamlError) {
          .kind = UNEXPECTED_TOKEN,
          .got = token_kind_to_string (token.kind),
          .exp = token_kind_to_string (TOKEN_CLOSE_SEQ),
        }
      );

    if (token.kind != TOKEN_COMMA && token.kind != TOKEN_CLOSE_SEQ)
      parser_error (
        yp,
        (YamlError) {
          .kind = UNEXPECTED_TOKEN,
          .got = token_kind_to_string (token.kind),
          .exp = token_kind_to_string (TOKEN_CLOSE_SEQ),
        }
      );

    sequence_add (&node->sequence, item);
    if (token.kind == TOKEN_CLOSE_SEQ) break;
  }

  return node;
}

static Node* parse_map (YamlParser* yp) {
  Node* node = create_node (NODE_MAP);

  Vector merge_maps = z3_vec (size_t);
  TokenKind expected_next = TOKEN_UNKNOWN;
  while (true) {
    Token token = next_token (yp);

    if (token.kind == TOKEN_CLOSE_MAP) break;
    token = peek_token (yp);

    if (token.kind == TOKEN_COMMA) {
      if (yp->root_mark == 0) {
        expected_next = TOKEN_KEY;
        goto unexpected_token_inloop;
      }
      token = next_token (yp);
    } else if (node->map.size > 0 && yp->root_mark > 0) {
      expected_next = TOKEN_COMMA;
      goto unexpected_token_inloop;
    }

    if (token.kind == TOKEN_EOF) {
      if (yp->root_mark == 0) break;
      parser_error (
        yp,
        (YamlError) {
          .kind = UNEXPECTED_TOKEN,
          .got = token_kind_to_string (token.kind),
          .exp = token_kind_to_string (TOKEN_CLOSE_MAP),
        }
      );
    }

    if (token.kind != TOKEN_KEY) {
      expected_next = TOKEN_KEY;
      goto unexpected_token_inloop;
    }

    char* key = token.raw;

    if (token.length == 2 && !memcmp (key, "<<", 2)) {
      // this is… not ideal, but doing this way, stops earlier
      char c = skip_all_whitespace (yp);
      free (key);

      if (c != CHAR_OPEN_BRACE && c != CHAR_ASTERISK) {
        token = next_token (yp);  // just needed for error
        parser_error (
          yp,
          (YamlError) {
            .kind = UNEXPECTED_TOKEN,
            .exp = "map or map alias",
            .got = token_kind_to_string (token.kind),
          }
        );
      }

      Node* value = parse_value (yp);

      if (value->kind != NODE_MAP) {
        parser_error (
          yp,
          (YamlError) {
            .kind = UNEXPECTED_TOKEN,
            .exp = "map",
            .got = node_kind_names[value->kind],
          }
        );
      }

      z3_push (merge_maps, value);
      continue;
    }

    Node* val = parse_value (yp);
    map_add (&node->map, key, val);

    continue;

  unexpected_token_inloop:
    // .exp can be token_kind_to_string (TOKEN_CLOSE_MAP)
    parser_error (
      yp,
      (YamlError) {
        .kind = UNEXPECTED_TOKEN,
        .got = token_kind_to_string (token.kind),
        .exp = token_kind_to_string (expected_next),
      }
    );
  }

  for (size_t i = 0; i < merge_maps.len; i++) {
    Node** val = (Node**)z3_get (merge_maps, i);
    (*val)->rcount--;
    for (size_t j = 0; j < (*val)->map.size; j++) {
      char* name = (*val)->map.entries[j].key;
      Node* value = (*val)->map.entries[j].val;
      value->rcount++;
      map_add (&node->map, name, value);
    }
  }
  z3_drop_vec (merge_maps);

  yp->root_mark--;
  return node;
}

Node* parse_value (YamlParser* yp) {
  Token token = next_token (yp);

  switch (token.kind) {
    case TOKEN_ANCHOR: {
      Node* value = parse_value (yp);
      YamlAlias ya = (YamlAlias) {nullptr, nullptr};
      ya.name = token.raw;
      ya.name[0] = '*';  // replace first `&` with `*`, to then match `*<alias>`
      ya.value = value;
      if (yp->aliases.length > 0) {
        if (parse_alias (yp, token) != NULL) {
          parser_error (
            yp,
            (YamlError) {// plz clang-format v22 in arch :sob:
                         .kind = REDEFINED_ALIAS,
                         .got = ya.name,
                         .exp = ""
            }
          );
        }
      };

      YamlAlias* temp =
        realloc (yp->aliases.items, (yp->aliases.length + 1) * sizeof (YamlAlias));

      if (!temp)
        die (
          "Out of memory allocating %zu bytes", (yp->aliases.length + 1) * sizeof (YamlAlias)
        );

      yp->aliases.items = temp;
      yp->aliases.items[yp->aliases.length] = ya;
      yp->aliases.length++;
      return value;
    }

    case TOKEN_ALIAS: {
      Node* value = parse_alias (yp, token);
      if (value) {
        value->rcount++;
        return value;
      }
      parser_error (yp, (YamlError) {.kind = UNDEFINED_ALIAS, .got = token.raw, .exp = ""});
    }

    case TOKEN_STRING:
    case TOKEN_STRING_LIT: {
      Node* node = create_node (NODE_STRING);
      node->string = token.raw;
      return node;
    }

    case TOKEN_NUMBER:
      return parse_number (token);

    case TOKEN_BOOLEAN: {
      Node* node = create_node (NODE_BOOLEAN);

      node->boolean = (token.length == 4);
      return node;
    }

    case TOKEN_OPEN_MAP:
      yp->root_mark++;
      return parse_map (yp);

    case TOKEN_OPEN_SEQ:
      return parse_seq (yp);

    default:
      parser_error (
        yp,
        (YamlError) {
          .kind = UNEXPECTED_TOKEN,
          .got = token_kind_to_string (token.kind),
          .exp = "a value",
        }
      );
  }
}

void free_yaml (Node* node) {
  free_node (node);
  node = nullptr;
}

Node* parse_yaml (const char* filepath) {
  int file_fd = fd_open_file (filepath);

  if (file_fd < 0) die ("could not open file %s: %s\n", filepath, strerror (errno));

  char input_buf[YAML_CHUNK_SIZE];

  YamlParser yp = {};
  yp.buff = input_buf;
  yp.ifd = file_fd;
  yp.cpos = 0;
  yp.line = 0;
  yp.bred = 1;
  yp.aliases = (YamlAliasList) {.length = 0, .items = nullptr};

  Node* root = parse_map (&yp);
  if (!eof_reached (&yp)) {
    parser_error (
      &yp,
      (YamlError) {
        .kind = UNEXPECTED_TOKEN,
        .got = token_kind_to_string (yp.cur_token.kind),
        .exp = token_kind_to_string (TOKEN_KEY),
      }
    );
  }

  if (yp.aliases.length > 0) {
    while (yp.aliases.length-- > 0) {
      free (yp.aliases.items[yp.aliases.length].name);
    }
    free (yp.aliases.items);
  }

  return root;
}

Node* map_get_node (Node* node, const char* key) {
  if (!node || node->kind != NODE_MAP) {
    errpfmt ("not a map\n");
    return nullptr;
  }

  for (size_t i = 0; i < node->map.size; i++) {
    if (strcmp (node->map.entries[i].key, key) == 0) {
      return node->map.entries[i].val;
    }
  }

  return nullptr;
}

const char* const token_kind_strings[] = {
  "TOKEN_UNKNOWN",     // 0
  "TOKEN_KEY",         // 1
  "TOKEN_STRING",      // 2
  "TOKEN_STRING_LIT",  // 3
  "TOKEN_NUMBER",      // 4
  "TOKEN_BOOLEAN",     // 5
  "TOKEN_COMMA",       // 7
  "TOKEN_ANCHOR",      // 9
  "TOKEN_ALIAS",       // 10
  "TOKEN_OPEN_MAP",    // 11
  "TOKEN_CLOSE_MAP",   // 12
  "TOKEN_OPEN_SEQ",    // 13
  "TOKEN_CLOSE_SEQ",   // 14
  "TOKEN_EOF",         // 15
  "TOKEN_MERGE",       // 16
  "TOKEN_INDENT",      // 17
  "TOKEN_DEDENT",      // 18
};

const char* node_kind_names[] = {
  [NODE_MAP] = "NODE_MAP",
  [NODE_SEQUENCE] = "NODE_SEQUENCE",
  [NODE_STRING] = "NODE_STRING",
  [NODE_NUMBER] = "NODE_NUMBER",
  [NODE_BOOLEAN] = "NODE_BOOLEAN"
};

// clang-format off
// Define valid character sets for both types
const uint8_t char_flags[256] = {
    ['0'] = 3, ['1'] = 3, ['2'] = 3, ['3'] = 3, ['4'] = 3, ['5'] = 3, ['6'] = 3,
    ['7'] = 3, ['8'] = 3, ['9'] = 3,

    ['<'] = 2, [':'] = 2,  // add all symbols allowed in YAML keys, all with 2 as value

    ['a'] = 2, ['b'] = 2, ['c'] = 2, ['d'] = 2, ['e'] = 3, ['f'] = 2, ['g'] = 2,
    ['h'] = 2, ['i'] = 2, ['j'] = 2, ['k'] = 2, ['l'] = 2, ['m'] = 2, ['n'] = 2,
    ['o'] = 2, ['p'] = 2, ['q'] = 2, ['r'] = 2, ['s'] = 2, ['t'] = 2, ['u'] = 2,
    ['v'] = 2, ['w'] = 2, ['x'] = 2, ['y'] = 2, ['z'] = 2,

    ['A'] = 2, ['B'] = 2, ['C'] = 2, ['D'] = 2, ['E'] = 3, ['F'] = 2, ['G'] = 2,
    ['H'] = 2, ['I'] = 2, ['J'] = 2, ['K'] = 2, ['L'] = 2, ['M'] = 2, ['N'] = 2,
    ['O'] = 2, ['P'] = 2, ['Q'] = 2, ['R'] = 2, ['S'] = 2, ['T'] = 2, ['U'] = 2,
    ['V'] = 2, ['W'] = 2, ['X'] = 2, ['Y'] = 2, ['Z'] = 2,

    ['_'] = 2, ['-'] = 3, ['.'] = 3, ['+'] = 3,
};
// clang-format on

#ifdef _YAML_TEST

#define node_kind_to_string(kind) node_kind_names[kind]

#define token_dbg(t, token)                                                              \
  {                                                                                      \
    char* v = token_value (t, token);                                                    \
    printf (                                                                             \
      "TOKEN: ~%3zu %36s '%s'\n", (token).length, token_kind_to_string ((token).kind), v \
    );                                                                                   \
    free (v);                                                                            \
  }

[[clang::always_inline]] static const char* node_value (Node* node) {
  static char _buf[20];  // NOLINT (readability-magic-numbers)

  switch (node->kind) {
    case NODE_STRING:
      return node->string;
    case NODE_NUMBER:
      snprintf (_buf, sizeof (_buf), "%f", node->number);
      return _buf;
    case NODE_BOOLEAN:
      return (int)node->boolean ? "true" : "false";
    default:
      return "UNKNOWN";
  }
}

void map_walk (Node* node, int indent);
static void seq_walk (Node* node, int indent) {
  for (size_t i = 0; i < node->sequence.size; i++) {
    Node* value = node->sequence.items[i];
    if (value->kind == NODE_MAP) {
      printf (
        "\x1b[1;36m%*s[%zu]{%zu}%s:\x1b[0m\n",
        indent,
        "",
        i,
        value->map.size,
        node_kind_to_string (value->kind)
      );
      map_walk (value, indent + 2);
      continue;
    }
    if (value->kind == NODE_SEQUENCE) {
      printf (
        "%*s\x1b[1;34m[%zu]{%zu}%s:\x1b[0m\n",
        indent,
        "",
        i,
        value->map.size,
        node_kind_to_string (value->kind)
      );
      seq_walk (value, indent + 2);
      continue;
    }
    printf (
      "%*s\x1b[1;32m[%zu]%s:\x1b[0m %s\n",
      indent,
      "",
      i,
      node_kind_to_string (value->kind),
      node_value (value)
    );
  }
}

void map_walk (Node* node, int indent) {
  for (size_t i = 0; i < node->map.size; i++) {
    char* name = node->map.entries[i].key;
    Node* value = node->map.entries[i].val;
    if (value->kind == NODE_MAP) {
      printf (
        "\x1b[1;36m%*s[%zu]{%zu}%s:\x1b[0m %s\n",
        indent,
        "",
        i,
        value->map.size,
        node_kind_to_string (value->kind),
        name
      );
      map_walk (value, indent + 2);
      continue;
    }
    if (value->kind == NODE_SEQUENCE) {
      printf (
        "%*s\x1b[1;34m[%zu]{%zu}%s:\x1b[0m %s\n",
        indent,
        "",
        i,
        value->map.size,
        node_kind_to_string (value->kind),
        name
      );
      seq_walk (value, indent + 2);
      continue;
    }
    printf (
      "%*s\x1b[1;32m[%zu]%s:\x1b[0m %s = %s\n",
      indent,
      "",
      i,
      node_kind_to_string (value->kind),
      name,
      node_value (value)
    );
  }
}

// this main function is just for debug
// this runs the tokenizer and prints all tokens
// like:
// TOKEN: ~  7             TOKEN_KEY 'package'
// The tokenizer does not allocate any memory,
// neither it modifies its input text, it just
// returns a token with start position and length info (slice)
// therefore it does not need any malloc nor free
int main (int argc, char** argv) {
  IGNORE_UNUSED (char* _this_file = popf (argc, argv));
  char* file_name = popf (argc, argv);

  int file_fd = fd_open_file (file_name);
  if (file_fd < 0) die ("could not open file %s: %s\n", file_name, strerror (errno));
  char input_buf[YAML_CHUNK_SIZE];
  YamlParser yp = {};
  yp.buff = input_buf;
  yp.cpos = 0;
  yp.line = 1;
  yp.aliases = (YamlAliasList) {.length = 0, .items = nullptr};
  while (true) {
    Token token = next_token (&yp);
    printf (
      "TOKEN: ~%3hu %36s '%s'\n", (token).length, token_kind_strings[(token).kind], token.raw
    );
    if (token.kind == TOKEN_UNKNOWN) break;
    if (token.kind == TOKEN_EOF) break;
  }
  printf ("\n");

  // Node* root = parse_yaml (file_name);
  //
  // if (!root) {
  //   errpfmt ("Failed to parse YAML\n");
  //   return 1;
  // }
  //
  // map_walk (root, 0);
  // free_yaml (root);

  return 0;
}
#endif
