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

extern const char* const token_kind_strings[];
const char* node_kind_names[];
extern const uint8_t char_flags[256];
static Node* parse_value (YamlParser* yp);

#define token_kind_to_string(kind) token_kind_strings[kind]
#define is_number_parseable(c)     (isdigit (c) || (c) == '.' || (c) == '-' || (c) == '+')
#define is_valid_anchor(c)         (isalnum (c) || (c) == '_' || (c) == '-')
#define is_valid_delim(c)                                                                      \
  ((c) == CHAR_SPACE || (c) == CHAR_NEWLINE || (c) == CHAR_COMMA || (c) == CHAR_CLOSE_BRACE || \
   (c) == CHAR_CLOSE_BRACKET)
#define create_token(k, v, l)              \
  (Token) {                                \
    .kind = (k), .raw = (v), .length = (l) \
  }

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
  ssize_t n = read (yp->iffd, yp->chunk, YAML_CHUNK_SIZE);
  // NOLINTNEXTLINE (concurrency-mt-unsafe)
  if (n < 0) die ("could not continue reading file: %s", strerror (errno));

  yp->cpos = 0;
  yp->blen = (uint16_t)n;
  yp->lred = (uint16_t)n;
  return n;
}

// Peek at current character without advancing the position
[[clang::always_inline]]
static char peek_char (YamlParser* yp) {
  return yp->chunk[yp->cpos];
}

[[clang::always_inline]]
static Token peek_token (YamlParser* yp) {
  return yp->cur_token;
}

[[clang::always_inline]]
static bool eof_reached (YamlParser* yp) {
  return (yp->blen > 0 && yp->cpos >= yp->blen) != 0;
}

[[clang::always_inline]]
static void skip_char (YamlParser* yp) {
  if (peek_char (yp) == CHAR_NEWLINE) {
    yp->line++;
    yp->lpos = 0;
  } else {
    yp->lpos++;
  }
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
    if (eof_reached (yp)) return;
    skip_char (yp);
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
      if (++yp->cpos >= yp->blen) {
        if (eof_reached (yp)) break;
        refill_buffers (yp);
      }
      yp->line++;
    } while (peek_char (yp) == CHAR_NEWLINE);
  }
  return CHAR_EOF;
}

static Token try_string_unesc (YamlParser* yp) {
  skip_char (yp);  // skip opening quote

  ScopedString s = z3_str (HEAP_VALUE_MIN_SIZE);
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

  ScopedString unesc = z3_unescape (s.chr, s.len);

  size_t start_offset = yp->strline->len;
  z3_pushl (yp->strline, unesc.chr, unesc.len);
  z3_pushc (yp->strline, 0);

  skip_char (yp);  // skip closing quote

  return (
    yp->cur_token =
      create_token (TOKEN_STRING, yp->strline->chr + start_offset, (uint32_t)unesc.len)
  );
}

static Token try_string_lit (YamlParser* yp) {
  size_t start_offset = yp->strline->len;
  char ilubsm[STACK_VALUE_BUFFER_SIZE];
  uint16_t len = 0;

  while (peek_char (yp) == CHAR_QUOTE_SINGLE) {
    skip_char (yp);  // open quote, or the second one of ''

    // skip until boundary
    while (!eof_reached (yp) && peek_char (yp) != CHAR_QUOTE_SINGLE &&
           peek_char (yp) != CHAR_NEWLINE) {
      if (len >= STACK_VALUE_BUFFER_SIZE) {
        z3_pushl (yp->strline, ilubsm, len);
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
      z3_pushl (yp->strline, ilubsm, len);
      len = 0;
    }
    ilubsm[len++] = peek_char (yp);
  }
  if (len > 0) z3_pushl (yp->strline, ilubsm, len);

  uint32_t token_len = (uint32_t)(yp->strline->len - start_offset);
  z3_pushc (yp->strline, 0);

  return (
    yp->cur_token = create_token (TOKEN_STRING_LIT, yp->strline->chr + start_offset, token_len)
  );
}

static Token try_anchor_or_alias (YamlParser* yp, char prefix) {
  char ilubsm[STACK_VALUE_BUFFER_SIZE];
  uint16_t buf_len = 0;
  size_t start_offset = yp->strline->len;
  uint32_t len = 0;

  while (!eof_reached (yp) && is_valid_anchor (peek_char (yp))) {
    if (buf_len >= STACK_VALUE_BUFFER_SIZE) {
      z3_pushl (yp->strline, ilubsm, buf_len);
      buf_len = 0;
    }
    ilubsm[buf_len++] = peek_char (yp);
    skip_char (yp);
    len++;
  }

  if (buf_len > 0) z3_pushl (yp->strline, ilubsm, buf_len);
  z3_pushc (yp->strline, 0);

  TokenKind kind = (prefix == '&') ? TOKEN_ANCHOR : TOKEN_ALIAS;
  return (yp->cur_token = create_token (kind, yp->strline->chr + start_offset, len));
}

static bool try_number (YamlParser* yp) {
  char ilubsm[STACK_VALUE_BUFFER_SIZE];
  uint16_t len = 0;

  while (!eof_reached (yp) && is_number_parseable (peek_char (yp))) {
    if (len >= STACK_VALUE_BUFFER_SIZE) {
      z3_pushl (yp->strline, ilubsm, len);
      len = 0;
    }
    ilubsm[len++] = peek_char (yp);
    skip_char (yp);
  }

  char c = peek_char (yp);
  if ((int)eof_reached (yp) || is_valid_delim (c)) {
    // definitively a number
    if (len > 0) z3_pushl (yp->strline, ilubsm, len);
    z3_pushc (yp->strline, 0);
    return true;
  }

  return false;
}

static int try_boolean (YamlParser* yp) {
  const char* pattern = (peek_char (yp) == 't') ? "true" : "false";

  for (size_t i = 0; pattern[i] != '\0'; i++) {
    if ((int)eof_reached (yp) || peek_char (yp) != pattern[i]) {
      z3_pushl (yp->strline, pattern, i);  // trx -> i == 2 -> push tr and leaves x
      return -1;
    }
    skip_char (yp);
  }

  // NOLINTNEXTLINE (readability-magic-numbers) 4 and 5 are obvious here
  int which_bool = pattern[4] == '\0';

  // z3_pushl (s, pattern, pattern[4] == '\0' ? 4 : 5);

  if (eof_reached (yp)) return which_bool;

  char c = peek_char (yp);
  if (is_valid_delim (c)) return which_bool;

  return -1;
}

static Token try_key (YamlParser* yp, size_t start_offset) {
  char ilubsm[STACK_VALUE_BUFFER_SIZE];
  uint16_t len = 0;

  // First character already peeked by caller, collect it
  if (len >= STACK_VALUE_BUFFER_SIZE) {
    z3_pushl (yp->strline, ilubsm, len);
    len = 0;
  }
  ilubsm[len++] = peek_char (yp);
  skip_char (yp);

  while (!eof_reached (yp)) {
    if (peek_char (yp) == CHAR_COLON) {
      skip_char (yp);
      if ((int)eof_reached (yp) || peek_char (yp) == CHAR_SPACE) break;

      if (len >= STACK_VALUE_BUFFER_SIZE) {
        z3_pushl (yp->strline, ilubsm, len);
        len = 0;
      }
      ilubsm[len++] = CHAR_COLON;
      continue;
    }

    if (len >= STACK_VALUE_BUFFER_SIZE) {
      z3_pushl (yp->strline, ilubsm, len);
      len = 0;
    }
    ilubsm[len++] = peek_char (yp);
    skip_char (yp);
  }

  if (len > 0) z3_pushl (yp->strline, ilubsm, len);

  uint32_t token_len = (uint32_t)(yp->strline->len - start_offset);
  z3_pushc (yp->strline, 0);

  return (yp->cur_token = create_token (TOKEN_KEY, yp->strline->chr + start_offset, token_len));
}

static Token next_token (YamlParser* yp) {
  char prefix_char = 0;
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
    case CHAR_ASTERISK:
      prefix_char = c;
      skip_char (yp);
      goto go_back_to_start;

    case CHAR_COMMA:
    case CHAR_OPEN_BRACE:
    case CHAR_OPEN_BRACKET:
    case CHAR_CLOSE_BRACE:
    case CHAR_CLOSE_BRACKET: {
      TokenKind type;
      switch (c) { /* NOLINT (bugprone-switch-missing-default-case) literally never */
        case CHAR_COMMA:
          type = TOKEN_COMMA;
          break;
        case CHAR_OPEN_BRACE:
          type = TOKEN_OPEN_MAP;
          break;
        case CHAR_OPEN_BRACKET:
          type = TOKEN_OPEN_SEQ;
          break;
        case CHAR_CLOSE_BRACE:
          type = TOKEN_CLOSE_MAP;
          break;
        case CHAR_CLOSE_BRACKET:
          type = TOKEN_CLOSE_SEQ;
          break;
      }
      yp->cur_token = (Token) {.kind = (type), .raw = nullptr, .length = 1};
      skip_char (yp);
      return yp->cur_token;
    }

    case CHAR_QUOTE_DOUBLE:
      return try_string_unesc (yp);

    case CHAR_QUOTE_SINGLE:
      return try_string_lit (yp);

    default:
      if (eof_reached (yp)) goto go_back_to_start;

      if (prefix_char != 0) {
        return try_anchor_or_alias (yp, prefix_char);
      }

      size_t start_offset = yp->strline->len;
      if (is_number_parseable (c)) {
        if (try_number (yp)) {
          return (Token) {.kind = TOKEN_NUMBER,
                          .raw = yp->strline->chr + start_offset,
                          .length = (uint32_t)(yp->strline->len - start_offset - 1)};
        }
      } else if (c == 't' || c == 'f') {
        int is_bool = try_boolean (yp);
        if (is_bool != -1)
          return (Token) {.kind = TOKEN_BOOLEAN, .raw = nullptr, .length = (uint32_t)is_bool};
      }

      return try_key (yp, start_offset);
  }
}

static Node* create_node (NodeKind kind) {
  Node* node = (Node*)malloc (sizeof (Node));

  if (!node) die ("Out of memory allocating %zu bytes", sizeof (Node));

  node->kind = kind;
  node->rcount = 0;

  switch (kind) {
    case NODE_MAP:
      node->map.size = 0;
      node->map.capacity = NODE_INITIAL_CAPACITY;
      node->map.entries = (MapEntry*)malloc (sizeof (MapEntry) * NODE_INITIAL_CAPACITY);
      if (!node->map.entries) {
        free (node);
        die ("Out of memory allocating %zu bytes", sizeof (MapEntry) * NODE_INITIAL_CAPACITY);
      }
      break;

    case NODE_SEQUENCE:
      node->sequence.size = 0;
      node->sequence.capacity = NODE_INITIAL_CAPACITY;
      node->sequence.items = (Node**)malloc (sizeof (Node*) * NODE_INITIAL_CAPACITY);
      if (!node->sequence.items) {
        die ("Out of memory allocating %zu bytes", sizeof (Node*) * NODE_INITIAL_CAPACITY);
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

static void free_node (Node* node) {
  if (!node) return;
  if (node->rcount > 0) {
    node->rcount--;
    return;
  }

  switch (node->kind) {
    case NODE_MAP:
      for (size_t i = 0; i < node->map.size; i++) {
        free_node (node->map.entries[i].val);
      }
      free (node->map.entries);
      break;

    case NODE_SEQUENCE:
      for (size_t i = 0; i < node->sequence.size; i++) {
        free_node (node->sequence.items[i]);
      }
      free ((void*)node->sequence.items);
      break;

    case NODE_STRING:  // not needed to
    // No dynamic memory to free
    case NODE_NUMBER:
    case NODE_BOOLEAN:
      break;
  }

  free (node);
  node = nullptr;
}

static void map_add (Map* map, char* key, Node* val) {
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

static void sequence_add (Sequence* seq, Node* item) {
  if (seq->size >= seq->capacity) {
    seq->capacity *= 2;
    Node** new_items = (Node**)realloc ((void*)seq->items, sizeof (Node*) * seq->capacity);

    if (!new_items) die ("Out of memory allocating %zu bytes", sizeof (Node*) * seq->capacity);

    seq->items = new_items;
  }

  seq->items[seq->size] = item;
  seq->size++;
}

static Node* parse_alias (YamlParser* yp, const char* alias) {
  size_t aliases = yp->aliases.len;
  if (aliases == 0) return nullptr;

  for (size_t i = 0; i < aliases; i++) {
    YamlAlias* a = (YamlAlias*)z3_get (yp->aliases, i);
    if (strcmp (a->name, alias) == 0) {
      return a->value;
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

    if (token.length == 2 && !memcmp (token.raw, "<<", 2)) {
      // this is… not ideal, but doing this way, stops earlier
      char c = skip_all_whitespace (yp);

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

      for (size_t j = 0; j < value->map.size; j++) {
        if (value->rcount > 0) value->map.entries[j].val->rcount++;
        map_add (&node->map, value->map.entries[j].key, value->map.entries[j].val);
      }

      // all values are moved to this object
      // alias case, just unlink it, as childs are incremented
      if (value->rcount > 0) {
        value->rcount--;
      } else {
        free (value->map.entries);
        free (value);
      }

      continue;
    }

    Node* val = parse_value (yp);
    map_add (&node->map, token.raw, val);

    continue;

  unexpected_token_inloop:
    parser_error (
      yp,
      (YamlError) {
        .kind = UNEXPECTED_TOKEN,
        .got = token_kind_to_string (token.kind),
        .exp = token_kind_to_string (expected_next),
      }
    );
  }

  yp->root_mark--;
  return node;
}

Node* parse_value (YamlParser* yp) {
  Token token = next_token (yp);

  switch (token.kind) {
    case TOKEN_ANCHOR: {
      // prevents to add more than one anchor (`item: &this &that ["value"]`)
      if (yp->aliases.len > 0 &&
          ((YamlAlias*)z3_get (yp->aliases, yp->aliases.len - 1))->value == nullptr)
        parser_error (
          yp,
          (YamlError) {// plz clang-format v22 in arch :sob:
                       .kind = UNEXPECTED_TOKEN,
                       .got = token_kind_to_string (token.kind),
                       .exp = "a value"
          }
        );

      if (parse_alias (yp, token.raw) != nullptr) {
        parser_error (
          yp,
          (YamlError) {// plz clang-format v22 in arch :sob:
                       .kind = REDEFINED_ALIAS,
                       .got = token.raw,
                       .exp = ""
          }
        );
      }

      yp->aliases.len++;
      Node* value = parse_value (yp);
      yp->aliases.len--;

      z3_push (yp->aliases, ((YamlAlias) {token.raw, value}));
      return value;
    }

    case TOKEN_ALIAS: {
      Node* value = parse_alias (yp, token.raw);
      if (value == nullptr)
        parser_error (yp, (YamlError) {.kind = UNDEFINED_ALIAS, .got = token.raw, .exp = ""});

      value->rcount++;
      return value;
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

      node->boolean = (token.length == 1);
      return node;
    }

    case TOKEN_OPEN_MAP:
      yp->root_mark++;
      return parse_map (yp);

    case TOKEN_OPEN_SEQ:
      return parse_seq (yp);

    default:
      errpfmt ("unweachabwe :3\n");
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

Node* parse_yaml (const char* filepath, String* strs) {
  int file_fd = fd_open_file (filepath);
  z3_pushl (strs, filepath, strlen (filepath));
  z3_pushc (strs, 0);

  // NOLINTNEXTLINE (concurrency-mt-unsafe)
  if (file_fd < 0) die ("could not open file %s: %s\n", filepath, strerror (errno));

  char yaml_chunk[YAML_CHUNK_SIZE];

  YamlParser yp = {0};
  yp.chunk = yaml_chunk;
  yp.iffd = file_fd;
  yp.strline = strs;
  yp.aliases = z3_vec (YamlAlias);
  yp.aliases.max = 4;
  yp.aliases.val = calloc (4, sizeof (YamlAlias));

  refill_buffers (&yp);
  if (eof_reached (&yp)) die ("File is empty");

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

  z3_drop_vec (yp.aliases);
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

#define PRINT_TOKEN(tok)                                                       \
  do {                                                                         \
    printf (                                                                   \
      "\033[1;35mTOKEN\033[0m [\033[1;33m%3u\033[0m] \033[1;36m%-20s\033[0m ", \
      (tok).length,                                                            \
      token_kind_strings[(tok).kind]                                           \
    );                                                                         \
                                                                               \
    if ((tok).raw == NULL) {                                                   \
      printf ("\033[91m<NULL>\033[0m\n");                                      \
      break;                                                                   \
    }                                                                          \
                                                                               \
    /* Print hex */                                                            \
    printf ("\033[90mhex[\033[0m");                                            \
    for (uint32_t _i = 0; _i < (tok).length; _i++) {                           \
      printf ("\033[32m%02X\033[0m", (unsigned char)(tok).raw[_i]);            \
      if (_i < (tok).length - 1) printf (" ");                                 \
    }                                                                          \
    printf ("\033[90m]\033[0m ");                                              \
                                                                               \
    /* Print string */                                                         \
    printf ("\033[90mstr[\033[0m\033[1;37m");                                  \
    for (uint32_t _i = 0; _i < (tok).length; _i++) {                           \
      char c = (tok).raw[_i];                                                  \
      if (c >= 32 && c <= 126)                                                 \
        printf ("%c", c);                                                      \
      else                                                                     \
        printf ("\033[91m·\033[1;37m");                                        \
    }                                                                          \
    printf ("\033[0m\033[90m]\033[0m\n");                                      \
  } while (0)

#define RED                 "\x1b[31m"
#define RESET               "\x1b[0m"
#define BYTES_PER_DUMP_LINE 16

static void hex_dump (const unsigned char* buf, size_t len) {
  for (size_t off = 0; off < len; off += BYTES_PER_DUMP_LINE) { /* hex column */
    for (size_t i = 0; i < BYTES_PER_DUMP_LINE; i++) {
      if (off + i < len)
        printf ("%02X ", buf[off + i]);
      else
        printf (" ");
    }
    printf ("-> "); /* char column */
    for (size_t i = 0; i < BYTES_PER_DUMP_LINE; i++) {
      if (off + i < len) {
        unsigned char c = buf[off + i];
        if (isprint (c))
          putchar (c);
        else
          printf (RED "." RESET);
      } else {
        putchar (' ');
      }
    }
    putchar ('\n');
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
  char* filename = popf (argc, argv);
  char* hmmm = argv[0];

  ScopedString strs = z3_str (1024);
  if (hmmm) {
    int file_fd = fd_open_file (filename);
    if (file_fd < 0)
      die (
        "could not open file %s: %s\n", filename, strerror (errno)
      );  // NOLINT (concurrency-mt-unsafe)

    char input_buf[YAML_CHUNK_SIZE];
    z3_pushl (&strs, filename, strlen (filename));
    z3_pushc (&strs, 0);

    YamlParser yp = {};
    yp.strline = &strs;
    yp.chunk = input_buf;
    yp.iffd = file_fd;
    yp.cpos = 0;
    yp.line = 0;
    yp.aliases = z3_vec (YamlAlias);
    refill_buffers (&yp);
    while (true) {
      Token token = next_token (&yp);
      PRINT_TOKEN (token);
      if (token.kind == TOKEN_UNKNOWN) break;
      if (token.kind == TOKEN_EOF) break;
    }
    printf ("\n");
    strs.len = 0;  // reset
  }

  Node* root = parse_yaml (filename, &strs);

  if (!root) {
    errpfmt ("Failed to parse YAML\n");
    return 1;
  }

  printf ("Hex dump of String(%zu)\n\n", strs.len);
  hex_dump ((unsigned char*)strs.chr, strs.len);
  map_walk (root, 0);
  free_yaml (root);

  return 0;
}
#endif
