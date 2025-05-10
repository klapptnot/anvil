#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>
#include <z3_toys.h>

#include "paerr.c"

extern const char* const token_kind_strings[];
#define token_kind_to_string(kind) token_kind_strings[kind]

Token create_token (TokenKind kind, size_t start, size_t length, size_t line, size_t column) {
  Token token;
  token.kind = kind;
  token.start = start;
  token.length = length;
  token.line = line;
  token.column = column;
  return token;
}

// Peek at current character without advancing the position
[[clang::always_inline]] char peek_char (Tokenizer* tokenizer) {
  return tokenizer->input[tokenizer->cpos];
}

// #define skip_char(tokenizer)                               \
//   (tokenizer->input[tokenizer->cpos++] == CHAR_NEWLINE \
//        ? (tokenizer->line++, tokenizer->column = 1)        \
//        : (tokenizer->column++))

// Advance the input position, updating line and column tracking
[[clang::always_inline]] void skip_char (Tokenizer* tokenizer) {
  if (tokenizer->input[tokenizer->cpos++] == CHAR_NEWLINE) {
    tokenizer->line++;
    tokenizer->ccol = 1;
  } else {
    tokenizer->ccol++;
  }
}

// skip and count whitespace (excluding newlines)
void skip_whitespace (Tokenizer* tokenizer) {
  while (tokenizer->input[tokenizer->cpos] == CHAR_SPACE) {
    tokenizer->cpos++;
    tokenizer->ccol++;
  }
  if (tokenizer->input[tokenizer->cpos] == CHAR_TAB) {
    parser_error (
        tokenizer,
        (YamlError){
            .kind = TAB_INDENTATION,
            .pos = tokenizer->cpos,
            .len = 1,
            .got = "",
            .exp = "",
        }
    );
  }
}

// read the token value out of the input
char* token_value (Tokenizer* tokenizer, Token token) {
  if (token.kind == TOKEN_STRING_LIT || token.kind == TOKEN_STRING) {
    token.length -= 2;  // the wrapping quotes
    token.start += 1;   // move start after the first quote
  }

  // if (token.kind == TOKEN_STRING) {
  //   return unescape_str(tokenizer->input + token.start, token.length);
  // }

  char* slice = malloc (token.length + 1);
  if (!slice) return NULL;  //= Handle allocation failure

  memcpy (slice, tokenizer->input + token.start, token.length);
  slice[token.length] = '\0';

  return slice;
}

[[clang::always_inline]] Token peek_token (Tokenizer* tokenizer) {
  return tokenizer->cur_token;
}

Token next_token (Tokenizer* tokenizer) {
  int loop_track = 1;
  int alias_tag = TAG_NULL;
prevent_recursion_by_going_back_to_start:
  if (peek_char (tokenizer) == CHAR_EOF) {
    tokenizer->cur_token =
        create_token (TOKEN_EOF, tokenizer->cpos, 0, tokenizer->line, tokenizer->ccol);
    return tokenizer->cur_token;
  }

  skip_whitespace (tokenizer);
  char c = peek_char (tokenizer);
  int start_position = tokenizer->cpos;
  int start_line = tokenizer->line;
  int start_column = tokenizer->ccol;
  size_t length;

  switch (c) {
    case CHAR_NEWLINE:
      while (tokenizer->input[tokenizer->cpos] == CHAR_NEWLINE) {
        tokenizer->cpos++;
        tokenizer->line++;
      }
      tokenizer->ccol = 1;
      goto prevent_recursion_by_going_back_to_start;

    case CHAR_HASH:
      // read until end of line
      while (peek_char (tokenizer) != CHAR_NEWLINE && peek_char (tokenizer) != CHAR_EOF) {
        skip_char (tokenizer);
      }
      goto prevent_recursion_by_going_back_to_start;

    case CHAR_AMPERSAND:
      alias_tag = TAG_ANCHOR;
      skip_char (tokenizer);
      goto prevent_recursion_by_going_back_to_start;

    case CHAR_ASTERISK:
      alias_tag = TAG_ALIAS;
      skip_char (tokenizer);
      goto prevent_recursion_by_going_back_to_start;

    case CHAR_COLON:
      tokenizer->cur_token =
          create_token (TOKEN_COLON, tokenizer->cpos, 1, start_line, start_column);
      skip_char (tokenizer);
      return tokenizer->cur_token;

    case CHAR_COMMA:
      tokenizer->cur_token =
          create_token (TOKEN_COMMA, tokenizer->cpos, 1, start_line, start_column);
      skip_char (tokenizer);
      return tokenizer->cur_token;

    case CHAR_OPEN_BRACE:
      tokenizer->cur_token =
          create_token (TOKEN_OPEN_MAP, tokenizer->cpos, 1, start_line, start_column);
      skip_char (tokenizer);
      return tokenizer->cur_token;

    case CHAR_OPEN_BRACKET:
      tokenizer->cur_token =
          create_token (TOKEN_OPEN_SEQ, tokenizer->cpos, 1, start_line, start_column);
      skip_char (tokenizer);
      return tokenizer->cur_token;

    case CHAR_CLOSE_BRACE:
      tokenizer->cur_token =
          create_token (TOKEN_CLOSE_MAP, tokenizer->cpos, 1, start_line, start_column);
      skip_char (tokenizer);
      return tokenizer->cur_token;

    case CHAR_CLOSE_BRACKET:
      tokenizer->cur_token =
          create_token (TOKEN_CLOSE_SEQ, tokenizer->cpos, 1, start_line, start_column);
      skip_char (tokenizer);
      return tokenizer->cur_token;

    case CHAR_QUOTE_DOUBLE:
      skip_char (tokenizer);

      while (peek_char (tokenizer) != CHAR_QUOTE_DOUBLE &&
             peek_char (tokenizer) != CHAR_NEWLINE && peek_char (tokenizer) != CHAR_EOF) {
        if (peek_char (tokenizer) == '\\') skip_char (tokenizer);
        skip_char (tokenizer);
      }

      if (peek_char (tokenizer) != CHAR_QUOTE_DOUBLE) {
        eprintf (
            "Reached %s while looking for matching double quote.\n",
            peek_char (tokenizer) == CHAR_EOF ? "EOF" : "NEWLINE"
        );
        exit (EXIT_FAILURE);
      }

      tokenizer->cur_token = create_token (
          TOKEN_STRING, start_position, tokenizer->cpos - start_position + 1, start_line,
          start_column
      );

      skip_char (tokenizer);
      return tokenizer->cur_token;

    case CHAR_QUOTE_SINGLE:
      skip_char (tokenizer);

      while (peek_char (tokenizer) != CHAR_QUOTE_SINGLE &&
             peek_char (tokenizer) != CHAR_NEWLINE && peek_char (tokenizer) != CHAR_EOF) {
        skip_char (tokenizer);
      }

      if (peek_char (tokenizer) != CHAR_QUOTE_SINGLE) {
        eprintf (
            "Reached %s while looking for matching single quote.\n",
            peek_char (tokenizer) == CHAR_EOF ? "EOF" : "NEWLINE"
        );
        exit (EXIT_FAILURE);
      }

      tokenizer->cur_token = create_token (
          TOKEN_STRING_LIT, start_position, tokenizer->cpos - start_position + 1, start_line,
          start_column
      );

      skip_char (tokenizer);
      return tokenizer->cur_token;

    default:
      // Handle numbers
      if ((isdigit (c) || c == '.' || c == '-' || c == '+')) {
        skip_char (tokenizer);

        while (true) {
          c = peek_char (tokenizer);
          if (!isdigit (c) && c != '.' && c != 'e' && c != 'E' && c != '-' && c != '+' &&
              c != '_')
            break;
          skip_char (tokenizer);
        }

        tokenizer->cur_token = create_token (
            TOKEN_NUMBER, start_position, tokenizer->cpos - start_position, start_line,
            start_column
        );
        return tokenizer->cur_token;
      }

      // identifiers (keys)
      if (isalnum (c) || c == '_' || c == '-' || c == '.') {
        skip_char (tokenizer);

        // Read until invalid identifier character
        while (isalnum (peek_char (tokenizer)) || peek_char (tokenizer) == '_' ||
               peek_char (tokenizer) == '-' || peek_char (tokenizer) == '.') {
          skip_char (tokenizer);
        }

        // Check if it's a boolean
        length = tokenizer->cpos - start_position;
        if (peek_char (tokenizer) != CHAR_COLON &&
            ((length == 4 && strncmp (tokenizer->input + start_position, "true", 4) == 0) ||
             (length == 5 && strncmp (tokenizer->input + start_position, "false", 5) == 0))) {
          tokenizer->cur_token =
              create_token (TOKEN_BOOLEAN, start_position, length, start_line, start_column);
        } else {
          if (alias_tag == TAG_ANCHOR)
            tokenizer->cur_token = create_token (
                TOKEN_ANCHOR, start_position - 1, length + 1, start_line, start_column
            );
          else if (alias_tag == TAG_ALIAS)
            tokenizer->cur_token = create_token (
                TOKEN_ALIAS, start_position - 1, length + 1, start_line, start_column
            );
          else
            tokenizer->cur_token =
                create_token (TOKEN_KEY, start_position, length, start_line, start_column);
        }

        return tokenizer->cur_token;
      }

      if (peek_char (tokenizer) == CHAR_EOF || loop_track--)
        goto prevent_recursion_by_going_back_to_start;

      tokenizer->cur_token =
          create_token (TOKEN_UNKNOWN, tokenizer->cpos, 1, start_line, start_column);
      return tokenizer->cur_token;
  }
}

Node* create_node (NodeKind kind) {
  Node* node = (Node*)malloc (sizeof (Node));
  if (!node) {
    eprintf ("Out of memory allocating %zu bytes", sizeof (Node));
    exit (EXIT_FAILURE);
  };

  node->kind = kind;
  node->rcount = 1;

  switch (kind) {
    case NODE_MAP:
      node->map.size = 0;
      node->map.capacity = 8;
      node->map.entries = (MapEntry*)malloc (sizeof (MapEntry) * node->map.capacity);
      if (!node->map.entries) {
        free (node);
        eprintf ("Out of memory allocating %zu bytes", sizeof (MapEntry) * node->map.capacity);
        exit (EXIT_FAILURE);
      }
      break;

    case NODE_SEQUENCE:
      node->sequence.size = 0;
      node->sequence.capacity = 8;
      node->sequence.items = (Node**)malloc (sizeof (Node*) * node->sequence.capacity);
      if (!node->sequence.items) {
        free (node);
        eprintf (
            "Out of memory allocating %zu bytes", sizeof (Node*) * node->sequence.capacity
        );
        exit (EXIT_FAILURE);
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
      free (node->string);
      node->string = NULL;
      break;

    case NODE_MAP:
      for (size_t i = 0; i < node->map.size; i++) {
        free (node->map.entries[i].key);
        free_node (node->map.entries[i].val);
      }
      free (node->map.entries);
      break;

    case NODE_SEQUENCE:
      for (size_t i = 0; i < node->sequence.size; i++) {
        free_node (node->sequence.items[i]);
      }
      free (node->sequence.items);
      break;

    // No dynamic memory to free
    case NODE_NUMBER:
    case NODE_BOOLEAN:
      break;
  }

  free (node);
  node = NULL;
}

void map_add (Map* map, char* key, Node* val) {
  if (map->size >= map->capacity) {
    map->capacity *= 2;
    MapEntry* new_entries =
        (MapEntry*)realloc (map->entries, sizeof (MapEntry) * map->capacity);
    if (!new_entries) {
      eprintf ("Out of memory allocating %zu bytes", sizeof (MapEntry) * map->capacity);
      exit (EXIT_FAILURE);
    }
    map->entries = new_entries;
  }

  map->entries[map->size].key = key;
  map->entries[map->size].val = val;
  map->size++;
}

void sequence_add (Sequence* seq, Node* item) {
  if (seq->size >= seq->capacity) {
    seq->capacity *= 2;
    Node** new_items = (Node**)realloc (seq->items, sizeof (Node*) * seq->capacity);
    if (!new_items) {
      eprintf ("Out of memory allocating %zu bytes", sizeof (Node*) * seq->capacity);
      exit (EXIT_FAILURE);
    };
    seq->items = new_items;
  }

  seq->items[seq->size] = item;
  seq->size++;
}

Node* parse_alias (Tokenizer* tokenizer, Token token) {
  size_t aliases = tokenizer->aliases.length;
  if (aliases == 0) return NULL;
  char* alias_name = token_value (tokenizer, token);
  alias_name[0] = '*';
  for (size_t i = 0; i < aliases; i++) {
    if (strcmp (tokenizer->aliases.items[i].name, alias_name) == 0) {
      free (alias_name);
      return tokenizer->aliases.items[i].value;
    }
  }
  free (alias_name);
  return NULL;
}

Node* parse_string (Tokenizer* tokenizer, Token token) {
  Node* node = create_node (NODE_STRING);

  node->string = token_value (tokenizer, token);
  return node;
}

Node* parse_number (Tokenizer* tokenizer, Token token) {
  Node* node = create_node (NODE_NUMBER);

  char* ver_value = malloc (token.length);
  if (ver_value == NULL) {
    eprintf ("Out of memory allocating %zu bytes", token.length);
    exit (EXIT_FAILURE);
  };

  char* value = token_value (tokenizer, token);
  size_t i = 0;
  size_t l = 0;
  while (i++ <= token.length) {
    // Allowing `1_000_000` to be `1000000`
    if (value[i] == '_') continue;
    ver_value[l++] = value[i];
  }

  node->number = atof (value);
  free (ver_value);
  free (value);
  return node;
}

Node* parse_boolean (Tokenizer* tokenizer, Token token) {
  Node* node = create_node (NODE_BOOLEAN);

  char* value = token_value (tokenizer, token);
  node->boolean = (token.length == 4);
  free (value);
  return node;
}

Node* parse_seq (Tokenizer* tokenizer) {
  Node* node = create_node (NODE_SEQUENCE);

  int loop_c = 0;
  // Don't consume tokens, loop once more
  // Expects to close or have a value, and
  // parse_value consumes tokens
  while (true) {
    Node* item = parse_value (tokenizer);

    if (item) {
      loop_c = 0;
      sequence_add (&node->sequence, item);
      continue;
    }

    Token token = peek_token (tokenizer);

    if (peek_token (tokenizer).kind == TOKEN_COMMA) {
      if (loop_c++) {
        parser_error (
            tokenizer,
            (YamlError){
                .kind = UNEXPECTED_TOKEN,
                .pos = token.start,
                .len = token.length,
                .got = token_kind_to_string (TOKEN_COMMA),
                .exp = "a value",
            }
        );
      }
      continue;
    }

    token = peek_token (tokenizer);

    if (token.kind == TOKEN_EOF || token.kind == TOKEN_CLOSE_SEQ) {
      break;
    }

    // 2 errors can be here, either expected value or close_seq
    parser_error (
        tokenizer,
        (YamlError){
            .kind = UNEXPECTED_TOKEN,
            .pos = token.start,
            .len = token.length,
            .got = token_kind_to_string (token.kind),
            .exp = token_kind_to_string (TOKEN_CLOSE_SEQ),
        }
    );
  }

  return node;
}

Node* parse_map (Tokenizer* tokenizer) {
  Node* node = create_node (NODE_MAP);

  while (true) {
    Token token = next_token (tokenizer);

    if (token.kind == TOKEN_COMMA && next_token (tokenizer).kind == TOKEN_COMMA) {
      parser_error (
          tokenizer,
          (YamlError){
              .kind = UNEXPECTED_TOKEN,
              .pos = token.start,
              .len = token.length,
              .got = token_kind_to_string (TOKEN_COMMA),
              .exp = "a key",
          }
      );
    } else {
      token = peek_token (tokenizer);
    }

    if (token.kind == TOKEN_EOF || token.kind == TOKEN_CLOSE_MAP) {
      break;
    };

    if (token.kind == TOKEN_KEY) {
      token = peek_token (tokenizer);
      Token colon_token = next_token (tokenizer);
      if (colon_token.kind != TOKEN_COLON) {
        parser_error (
            tokenizer,
            (YamlError){
                .kind = UNEXPECTED_TOKEN,
                .pos = colon_token.start,
                .len = colon_token.length,
                .got = token_kind_to_string (colon_token.kind),
                .exp = token_kind_to_string (TOKEN_COLON),
            }
        );
      }

      Node* value_node = parse_value (tokenizer);
      if (!value_node) {
        break;
      }

      map_add (&node->map, token_value (tokenizer, token), value_node);
      continue;
    }

    parser_error (
        tokenizer,
        (YamlError){
            .kind = UNEXPECTED_TOKEN,
            .pos = token.start,
            .len = token.length,
            .got = token_kind_to_string (token.kind),
            .exp = token_kind_to_string (TOKEN_CLOSE_MAP),
        }
    );
  }

  return node;
}

Node* parse_value (Tokenizer* tokenizer) {
  Token token = next_token (tokenizer);

  switch (token.kind) {
    case TOKEN_ANCHOR: {
      Node* value = parse_value (tokenizer);
      YamlAlias ya = (YamlAlias){0};
      ya.name = token_value (tokenizer, token);
      ya.name[0] = '*';  // replace first `&` with `*`, to then match `*<alias>`
      ya.value = value;
      if (tokenizer->aliases.length > 0) {
        if (parse_alias (tokenizer, token) != NULL) {
          parser_error (
              tokenizer, (YamlError){.kind = REDEFINED_ALIAS,
                                     .pos = token.start,
                                     .len = token.length,
                                     .got = ya.name,
                                     .exp = ""}
          );
        }
      };
      YamlAlias* temp = realloc (
          tokenizer->aliases.items, (tokenizer->aliases.length + 1) * sizeof (YamlAlias)
      );
      if (!temp) {
        eprintf (
            "Out of memory allocating %zu bytes",
            (tokenizer->aliases.length + 1) * sizeof (YamlAlias)
        );
        exit (EXIT_FAILURE);
      };

      tokenizer->aliases.items = temp;
      tokenizer->aliases.items[tokenizer->aliases.length] = ya;
      tokenizer->aliases.length++;
      return value;
    }

    case TOKEN_ALIAS: {
      Node* value = parse_alias (tokenizer, token);
      if (value) {
        value->rcount++;
        return value;
      }
      parser_error (
          tokenizer, (YamlError){.kind = UNDEFINED_ALIAS,
                                 .pos = token.start,
                                 .len = token.length,
                                 .got = token_value (tokenizer, token),
                                 .exp = ""}
      );
    }

    case TOKEN_STRING:
    case TOKEN_STRING_LIT:
      return parse_string (tokenizer, token);

    case TOKEN_NUMBER:
      return parse_number (tokenizer, token);

    case TOKEN_BOOLEAN:
      return parse_boolean (tokenizer, token);

    case TOKEN_OPEN_MAP:
      return parse_map (tokenizer);

    case TOKEN_OPEN_SEQ:
      return parse_seq (tokenizer);

    default:
      return NULL;
  }
}

void free_yaml (Node* node) {
  free_node (node);
  node = NULL;
}

Node* parse_yaml (const char* input) {
  Tokenizer tokenizer = {};
  tokenizer.input = input;
  tokenizer.cpos = 0;
  tokenizer.line = 1;
  tokenizer.ccol = 1;
  tokenizer.aliases = (YamlAliasList){.length = 0, .items = NULL};

  Node* root = parse_map (&tokenizer);
  if (tokenizer.input[tokenizer.cpos] != CHAR_EOF) {
    parser_error (
        &tokenizer,
        (YamlError){
            .kind = UNEXPECTED_TOKEN,
            .pos = tokenizer.cpos - 1,
            .len = tokenizer.cur_token.length,
            .got = token_kind_to_string (tokenizer.cur_token.kind),
            .exp = token_kind_to_string (TOKEN_KEY),
        }
    );
  }

  if (tokenizer.aliases.length > 0) {
    while (tokenizer.aliases.length-- > 0) {
      free (tokenizer.aliases.items[tokenizer.aliases.length].name);
    }
    free (tokenizer.aliases.items);
  }

  return root;
}

Node* map_get_node (Node* node, const char* key) {
  if (!node || node->kind != NODE_MAP) {
    eprintf ("not a map\n");
    return NULL;
  }

  for (size_t i = 0; i < node->map.size; i++) {
    if (strcmp (node->map.entries[i].key, key) == 0) {
      return node->map.entries[i].val;
    }
  }

  return NULL;
}

const char* const token_kind_strings[] = {
    "TOKEN_UNKNOWN",     // 0
    "TOKEN_KEY",         // 1
    "TOKEN_STRING",      // 2
    "TOKEN_STRING_LIT",  // 3
    "TOKEN_NUMBER",      // 4
    "TOKEN_BOOLEAN",     // 5
    "TOKEN_COLON",       // 6
    "TOKEN_COMMA",       // 7
    "TOKEN_NEWLINE",     // 8
    "TOKEN_ANCHOR",      // 9
    "TOKEN_ALIAS",       // 10
    "TOKEN_OPEN_MAP",    // 11
    "TOKEN_CLOSE_MAP",   // 12
    "TOKEN_OPEN_SEQ",    // 13
    "TOKEN_CLOSE_SEQ",   // 14
    "TOKEN_EOF",         // 15
    "TOKEN_INDENT",      // 16
    "TOKEN_DEDENT",      // 17
};

#ifdef __YAML_TEST
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

  FILE* file = fopen (file_name, "r");
  fseek (file, 0, SEEK_END);
  long length = ftell (file);
  fseek (file, 0, SEEK_SET);

  char* yaml_input = malloc (next_power_of2 (length + 1));
  IGNORE_UNUSED (fread (yaml_input, 1, length, file));
  yaml_input[length] = '\0';
  fclose (file);

  // Tokenizer tokenizer;
  // tokenizer_init (&tokenizer, yaml_input);
  // while (true) {
  //   Token token = next_token (&tokenizer);
  //   token_dbg (&tokenizer, token);
  //   if (token.kind == TOKEN_UNKNOWN) break;
  //   if (token.kind == TOKEN_EOF) break;
  // }
  // printf ("\n");

  Node* root = parse_yaml (yaml_input);

  if (!root) {
    eprintf ("Failed to parse YAML\n");
    return 1;
  }

  map_walk (root, 0);
  free_yaml (root);

  free (yaml_input);
  return 0;
}
#endif
