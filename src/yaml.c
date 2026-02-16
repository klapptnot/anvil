// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _YAML_TEST
#define Z3_TOYS_IMPL
#define Z3_STRING_IMPL
#endif
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
  yp->reof = n == 0;
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
  return yp->reof != 0;
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

static String* get_string_line (YamlParser* yp, size_t reqsz) {
  YamlStore* store = yp->store;

  for (size_t i = 0; i < store->strs.len; i++) {
    String* curstr = z3_get (store->strs, i);
    if (reqsz + 1 < curstr->max - curstr->len) return curstr;
    // if (reqsz + curstr->len < curstr->max) return curstr;
  }

  if (reqsz >= STR_ALLOC_SIZE_BASE) {
    String nz3s = z3_str (reqsz);
    size_t len = yp->store->bigs.len;

    z3_push (yp->store->bigs, nz3s);
    return z3_get (yp->store->bigs, len);
  }

  String nz3s = z3_str (STR_ALLOC_SIZE_BASE);
  z3_push (store->strs, nz3s);

  return z3_get (store->strs, store->strs.len - 1);
}

static Token try_string_unesc (YamlParser* yp) {
  skip_char (yp);  // skip opening quote

  ScopedString s = z3_str (HEAP_VALUE_MIN_SIZE);
  char ilubsm[STR_ALLOC_SIZE_BASE] = {0};
  uint16_t len = 0;

  while (!eof_reached (yp) && peek_char (yp) != CHAR_QUOTE_DOUBLE &&
         peek_char (yp) != CHAR_NEWLINE) {
    if (len >= STR_ALLOC_SIZE_BASE) {
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

  String unesc = z3_unescape (s.chr, s.len);
  z3_push (yp->store->bigs, unesc);

  skip_char (yp);  // skip closing quote

  return (yp->cur_token = create_token (TOKEN_STRING, unesc.chr, (uint32_t)unesc.len));
}

static Token try_string_lit (YamlParser* yp) {
  char ilubsm[STR_ALLOC_SIZE_BASE] = {0};
  uint16_t len = 0;
  String* hold = nullptr;

  while (peek_char (yp) == CHAR_QUOTE_SINGLE) {
    skip_char (yp);  // open quote, or the second one of ''

    // skip until boundary
    while (!eof_reached (yp) && peek_char (yp) != CHAR_QUOTE_SINGLE &&
           peek_char (yp) != CHAR_NEWLINE) {
      if (len > STR_ALLOC_SIZE_BASE) {
        hold = hold ? hold : get_string_line (yp, len);
        z3_pushl (hold, ilubsm, len);
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

    // either an '' or definite end, skip one
    skip_char (yp);
    // if eof, peek_char returns always the last char
    if (eof_reached (yp)) break;

    // if '', then continue after it
    if (peek_char (yp) != CHAR_QUOTE_SINGLE) break;
    if (len > STR_ALLOC_SIZE_BASE) {
      hold = hold ? hold : get_string_line (yp, len);
      z3_pushl (hold, ilubsm, len);
      len = 0;
    }
    ilubsm[len++] = peek_char (yp);
  }

  char* start = nullptr;
  uint32_t tlen = 0;

  if (hold) {
    start = hold->chr;
    if (len > 0) z3_pushl (hold, ilubsm, len);
    tlen = (uint32_t)hold->len;
  } else {
    hold = get_string_line (yp, len);
    size_t beginning = hold->len;
    if (len > 0) z3_pushl (hold, ilubsm, len);
    tlen = (uint32_t)(hold->len - beginning);
    z3_pushc (hold, 0);
    start = (hold->chr + beginning);
  }

  return (yp->cur_token = create_token (TOKEN_STRING_LIT, start, tlen));
}

static Token try_anchor_or_alias (YamlParser* yp, char prefix) {
  char ilubsm[STR_ALLOC_SIZE_BASE];
  uint32_t len = 0;
  String* hold = nullptr;

  while (!eof_reached (yp) && is_valid_anchor (peek_char (yp))) {
    if (len > STR_ALLOC_SIZE_BASE) {
      hold = hold ? hold : get_string_line (yp, len);
      z3_pushl (hold, ilubsm, len);
      len = 0;
    }
    ilubsm[len++] = peek_char (yp);
    skip_char (yp);
  }

  char* start = nullptr;
  uint32_t tlen = 0;

  if (hold) {
    if (len > 0) z3_pushl (hold, ilubsm, len);
    start = hold->chr;
    tlen = (uint32_t)hold->len;
  } else {
    hold = get_string_line (yp, len);
    size_t beginning = hold->len;
    if (len > 0) z3_pushl (hold, ilubsm, len);
    tlen = (uint32_t)(hold->len - beginning);
    z3_pushc (hold, 0);
    start = (hold->chr + beginning);
  }

  TokenKind kind = (prefix == '&') ? TOKEN_ANCHOR : TOKEN_ALIAS;
  return (yp->cur_token = create_token (kind, start, tlen));
}

static int try_number (YamlParser* yp, char* ilubsm) {
  uint32_t len = 0;
  while (!eof_reached (yp) && is_number_parseable (peek_char (yp))) {
    if (len >= MAX_NUMBER_LENGTH) {  // MAX_NUMBER_LENGTH
      parser_error (yp, (YamlError) {.kind = NUMBER_TOO_LONG, .got = "", .exp = ""});
    }
    ilubsm[len++] = peek_char (yp);
    skip_char (yp);
  }

  char c = peek_char (yp);
  return ((int)eof_reached (yp) || is_valid_delim (c)) ? (int)len : -1;
}

static int try_boolean (YamlParser* yp, char* ilubsm) {
  const char* pattern = (peek_char (yp) == 't') ? "true" : "false";
  skip_char (yp);
  uint32_t len = 1;

  for (; pattern[len] != '\0'; len++) {
    if ((int)eof_reached (yp) || peek_char (yp) != pattern[len]) {
      memcpy (ilubsm, pattern, len);  // trx -> i == 2 -> push tr and leaves x
      return (int)(len + 'x');
    }
    skip_char (yp);
  }

  // NOLINTNEXTLINE (readability-magic-numbers) 4 and 5 are obvious here
  int which_bool = pattern[4] == '\0';

  // z3_pushl (s, pattern, pattern[4] == '\0' ? 4 : 5);

  char c = peek_char (yp);
  if ((int)eof_reached (yp) || is_valid_delim (c)) return which_bool;

  return (int)(len + 'x');
}

static Token try_key (YamlParser* yp, char* ilubsm, uint32_t blen) {
  uint32_t len = blen;
  while (!eof_reached (yp)) {
    if (len >= STR_ALLOC_SIZE_BASE)
      parser_error (yp, (YamlError) {.kind = KEY_TOO_LONG, .exp = "", .got = ""});

    if (peek_char (yp) == CHAR_COLON) {
      skip_char (yp);
      if ((int)eof_reached (yp) || peek_char (yp) == CHAR_SPACE) break;

      ilubsm[len++] = CHAR_COLON;
      continue;
    }

    ilubsm[len++] = peek_char (yp);
    skip_char (yp);
  }

  if (len >= STR_ALLOC_SIZE_BASE)
    parser_error (yp, (YamlError) {.kind = KEY_TOO_LONG, .exp = "", .got = ""});

  String* hold = get_string_line (yp, len);
  char* start = hold->len == 0 ? hold->chr : hold->chr + hold->len;

  if (len > 0) z3_pushl (hold, ilubsm, len);
  z3_pushc (hold, 0);

  return (yp->cur_token = create_token (TOKEN_KEY, start, (uint32_t)len));
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

      char ilubsm[STR_ALLOC_SIZE_BASE];
      uint32_t len = 0;

      if (is_number_parseable (c)) {
        int tlen = try_number (yp, ilubsm);
        len = (uint32_t)tlen;

        if (tlen != -1) {
          String* hold = get_string_line (yp, len);

          char* start = hold->len == 0 ? hold->chr : hold->chr + hold->len;
          if (len > 0) z3_pushl (hold, ilubsm, len);
          z3_pushc (hold, 0);

          return create_token (TOKEN_NUMBER, start, (uint32_t)tlen);
        }
      } else if (c == 't' || c == 'f') {
        int is_bool = try_boolean (yp, ilubsm);
        if (is_bool < 'x') return create_token (TOKEN_BOOLEAN, nullptr, (uint32_t)is_bool);
        len = (uint32_t)(is_bool - 'x');
      }

      return try_key (yp, ilubsm, len);
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
      node->map.entries = (YamlMapEntry*)malloc (sizeof (YamlMapEntry) * NODE_INITIAL_CAPACITY);
      if (!node->map.entries) {
        free (node);
        die (
          "Out of memory allocating %zu bytes", sizeof (YamlMapEntry) * NODE_INITIAL_CAPACITY
        );
      }
      break;

    case NODE_LIST:
      node->list.size = 0;
      node->list.capacity = NODE_INITIAL_CAPACITY;
      node->list.items = (Node**)malloc (sizeof (Node*) * NODE_INITIAL_CAPACITY);
      if (!node->list.items) {
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

    case NODE_LIST:
      for (size_t i = 0; i < node->list.size; i++) {
        free_node (node->list.items[i]);
      }
      free ((void*)node->list.items);
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

static void map_add (YamlMap* map, char* key, Node* val) {
  if (map->size >= map->capacity) {
    map->capacity *= 2;
    YamlMapEntry* new_entries =
      (YamlMapEntry*)realloc (map->entries, sizeof (YamlMapEntry) * map->capacity);

    if (!new_entries)
      die ("Out of memory allocating %zu bytes", sizeof (YamlMapEntry) * map->capacity);

    map->entries = new_entries;
  }

  map->entries[map->size].key = key;
  map->entries[map->size].val = val;
  map->size++;
}

static void list_add (YamlList* seq, Node* item) {
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

static Node* parse_list (YamlParser* yp) {
  Node* node = create_node (NODE_LIST);

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

    list_add (&node->list, item);
    if (token.kind == TOKEN_CLOSE_SEQ) break;
  }

  return node;
}

static void parse_map (YamlParser* yp, Node* node) {
  yp->root_mark++;

  TokenKind expected_next = TOKEN_UNKNOWN;
  while (true) {
    Token token = next_token (yp);

    if (token.kind == TOKEN_CLOSE_MAP) break;
    token = peek_token (yp);

    if (token.kind == TOKEN_COMMA) {
      if (yp->root_mark == 1) {
        expected_next = TOKEN_KEY;
        goto unexpected_token_inloop;
      }
      token = next_token (yp);
    } else if (node->map.size > 0 && yp->root_mark > 1) {
      expected_next = TOKEN_COMMA;
      goto unexpected_token_inloop;
    }

    if (token.kind == TOKEN_EOF) {
      if (yp->root_mark == 1) break;
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

      if (c == CHAR_OPEN_BRACE) {  // literal map
        token = next_token (yp);   // consume token, as parse_value does
        parse_map (yp, node);      // just append to current map
        continue;
      }

      if (c == CHAR_ASTERISK) {  // alias, unknown type
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

        value->rcount--;

        continue;
      }

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
      Node* node = create_node (NODE_MAP);
      parse_map (yp, node);
      return node;

    case TOKEN_OPEN_SEQ:
      return parse_list (yp);

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

Node* parse_yaml (const char* filepath, YamlStore* store) {
  int file_fd = fd_open_file (filepath);

  store->strs = z3_vec (String);
  store->bigs = z3_vec (String);

  z3_vec_init_capacity (store->strs, 4);
  z3_vec_init_capacity (store->bigs, 4);

  String fst = z3_str (STR_ALLOC_SIZE_BASE);
  z3_pushl (&fst, filepath, strlen (filepath));
  z3_pushc (&fst, 0);
  z3_push (store->strs, fst);

  size_t curr_str_len = fst.len;

  // NOLINTNEXTLINE (concurrency-mt-unsafe)
  if (file_fd < 0) die ("could not open file %s: %s\n", filepath, strerror (errno));

  char yaml_chunk[YAML_CHUNK_SIZE];

  YamlParser yp = {0};
  yp.store = store;
  yp.chunk = yaml_chunk;
  yp.iffd = file_fd;

  // Initialize with 4 as size, since the push function starts with 32
  yp.aliases = z3_vec (YamlAlias);
  z3_vec_init_capacity (yp.aliases, 4);

  refill_buffers (&yp);
  if (eof_reached (&yp)) die ("File is empty");

  Token first = next_token (&yp);
  Node* root = nullptr;

  switch (first.kind) {
    case TOKEN_OPEN_SEQ:
      yp.root_mark++;  // any not global map is flow-style
      root = parse_list (&yp);
      break;

    case TOKEN_OPEN_MAP:
      yp.root_mark++;  // it is flow already
      root = create_node (NODE_MAP);
      parse_map (&yp, root);
      break;

    case TOKEN_KEY:
      // Backtrack since parse_map will consume the key
      yp.cpos = 0;
      yp.line = 0;
      yp.lpos = 0;

      if (((String*)z3_get (store->strs, 0))->len != curr_str_len) {
        ((String*)z3_get (store->strs, 0))->len = curr_str_len;
      } else {
        z3_drops ((String*)z3_get (store->strs, 1));
      }

      root = create_node (NODE_MAP);
      parse_map (&yp, root);
      break;

    default:
      // Backtrack and parse as single value
      yp.cpos = 0;
      yp.line = 0;
      yp.lpos = 0;

      if (((String*)z3_get (store->strs, 0))->len != curr_str_len) {
        ((String*)z3_get (store->strs, 0))->len = curr_str_len;
      } else if (store->strs.len == 2) {
        z3_drops ((String*)z3_get (store->strs, 1));
      } else {
        z3_drops ((String*)z3_get (store->bigs, 0));
      }

      root = parse_value (&yp);
      break;
  }

  first = next_token (&yp);  // last

  if (first.kind != TOKEN_EOF) {
    parser_error (
      &yp,
      (YamlError) {
        .kind = UNEXPECTED_TOKEN,
        .got = token_kind_to_string (yp.cur_token.kind),
        .exp = token_kind_to_string (TOKEN_EOF),
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
  [NODE_LIST] = "NODE_LIST",
  [NODE_STRING] = "NODE_STRING",
  [NODE_NUMBER] = "NODE_NUMBER",
  [NODE_BOOLEAN] = "NODE_BOOLEAN"
};

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
static void list_walk (Node* node, int indent) {
  for (size_t i = 0; i < node->list.size; i++) {
    Node* value = node->list.items[i];
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
    if (value->kind == NODE_LIST) {
      printf (
        "%*s\x1b[1;34m[%zu]{%zu}%s:\x1b[0m\n",
        indent,
        "",
        i,
        value->map.size,
        node_kind_to_string (value->kind)
      );
      list_walk (value, indent + 2);
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
    if (value->kind == NODE_LIST) {
      printf (
        "%*s\x1b[1;34m[%zu]{%zu}%s:\x1b[0m %s\n",
        indent,
        "",
        i,
        value->map.size,
        node_kind_to_string (value->kind),
        name
      );
      list_walk (value, indent + 2);
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

z3_vec_drop_fn (String, z3_drops);

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
  char* filepath = popf (argc, argv);
  char* hmmm = argv[0];

  if (hmmm) {
    YamlStore store;
    int file_fd = fd_open_file (filepath);

    store.strs = z3_vec (String);
    store.bigs = z3_vec (String);

    z3_vec_init_capacity (store.strs, 4);
    z3_vec_init_capacity (store.bigs, 4);

    String fst = z3_str (STR_ALLOC_SIZE_BASE);
    z3_pushl (&fst, filepath, strlen (filepath));
    z3_pushc (&fst, 0);
    z3_push (store.strs, fst);

    // NOLINTNEXTLINE (concurrency-mt-unsafe)
    if (file_fd < 0) die ("could not open file %s: %s\n", filepath, strerror (errno));

    char yaml_chunk[YAML_CHUNK_SIZE];

    YamlParser yp = {0};
    yp.store = &store;
    yp.chunk = yaml_chunk;
    yp.iffd = file_fd;

    // Initialize with 4 as size, since the push function starts with 32
    yp.aliases = z3_vec (YamlAlias);
    z3_vec_init_capacity (yp.aliases, 4);

    refill_buffers (&yp);
    if (eof_reached (&yp)) die ("File is empty");
    while (!eof_reached (&yp)) {
      Token token = next_token (&yp);
      PRINT_TOKEN (token);
      if (token.kind == TOKEN_UNKNOWN) break;
      if (token.kind == TOKEN_EOF) break;
    }
    z3_vec_drop_String (&store.strs);
    z3_vec_drop_String (&store.bigs);
    z3_drop_vec (yp.aliases);
    printf ("\n");
  }

  YamlStore stores;
  Node* root = parse_yaml (filepath, &stores);

  if (!root) {
    errpfmt ("Failed to parse YAML\n");
    return 1;
  }

#define z3__display_String(s) printf ("%s", (s)->chr)
  z3_vec_dbg (stores.strs);
  z3_vec_show (stores.bigs, String);

  for (size_t i = 0; i < stores.strs.len; i++) {
    String* strs = z3_get (stores.strs, i);
    printf ("Hex dump of String(%zu)\n\n", strs->len);
    hex_dump ((unsigned char*)strs->chr, strs->len);
  }

  if (root->kind == NODE_MAP)
    map_walk (root, 0);
  else if (root->kind == NODE_LIST)
    list_walk (root, 0);
  else
    printf (
      "\x1b[1;32m%s:\x1b[0m %s = %s\n", node_kind_to_string (root->kind), ".", node_value (root)
    );

  free_yaml (root);
  z3_vec_drop_String (&stores.strs);
  z3_vec_drop_String (&stores.bigs);

  return 0;
}
#endif
