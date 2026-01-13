// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <yaml.h>
#include <z3_string.h>
#include <z3_toys.h>

#define LINE_BUFFER_MAX_LENGTH 256
#define LINE_BUFFER_CLAMP(x)   ((x) > LINE_BUFFER_MAX_LENGTH ? LINE_BUFFER_MAX_LENGTH : (x))
#define COPY_LINE(buf, off, len)         \
  {                                      \
    size_t _l = LINE_BUFFER_CLAMP (len); \
    memcpy (buf, input + off, _l);       \
    buf[_l] = '\0';                      \
  }

static const char* yaml_error_to_string (YamlErrorKind error) {
  switch (error) {
    case TAB_INDENTATION:
      return "TAB_INDENTATION";
    case UNEXPECTED_TOKEN:
      return "UNEXPECTED_TOKEN";
    case WRONG_SYNTAX:
      return "WRONG_SYNTAX";
    case KEY_REDEFINITION:
      return "KEY_REDEFINITION";
    case UNDEFINED_ALIAS:
      return "UNDEFINED_ALIAS";
    case REDEFINED_ALIAS:
      return "REDEFINED_ALIAS";
    case MISSING_VALUE:
      return "MISSING_VALUE";
    case MISSING_COMMA:
      return "MISSING_COMMA";
    case UNCLOSED_QUOTE:
      return "UNCLOSED_QUOTE";
    default:
      return "UNKNOWN_ERROR";
  }
}

static bool parser_filler (String* res, void* ctx, char* item, size_t len) {
  YamlError* ctxs = (YamlError*)ctx;

  switch (ctxs->kind) {
    case TAB_INDENTATION:
      return false;
    case UNEXPECTED_TOKEN:
      if (strncmp (item, "exp", 3 > len ? len : 3) == 0)
        z3_pushl (res, ctxs->exp, strlen (ctxs->exp));
      else
        z3_pushl (res, ctxs->got, strlen (ctxs->got));
      return true;
    case WRONG_SYNTAX:
    case KEY_REDEFINITION:
    case UNDEFINED_ALIAS:
    case REDEFINED_ALIAS:
    case MISSING_VALUE:
      // key or alias name, or token found
      z3_pushl (res, ctxs->got, strlen (ctxs->got));
      return true;
    case MISSING_COMMA:
      return true;
    case UNCLOSED_QUOTE:
      if (strncmp (item, "exp", 3 > len ? len : 3) == 0)
        z3_pushl (res, ctxs->exp, strlen (ctxs->exp));
      else
        z3_pushl (res, ctxs->got, strlen (ctxs->got));
      return true;
    default:
      z3_pushlit (res, "Unknown error occurred.");
      return true;
  }
}

void parser_error (Tokenizer* tokenizer, YamlError error) {
  // Error messages corresponding to YamlError enum
  const char* yaml_error_messages[] = {
    "Tabs cannot be used for indentation.",
    "Expected #{exp}, found #{got}.",
    "Unexpected character.",
    "#{} is redefined in the current context.",
    "Alias #{} is undefined.",
    "Alias #{} is already defined.",
    "Missing value after key #{}.",
    "Comma missing between elements in a collection.",
    "Reached #{got} while looking for matching `#{exp}` quote."
  };

  const char* input = tokenizer->input;

  // find start of the error line
  size_t errln_start = error.pos;
  while (errln_start > 0 && input[errln_start - 1] != '\n') {
    errln_start--;
  }

  // find end of the error line
  size_t errln_end = error.pos;
  while (input[errln_end] != '\n' && input[errln_end] != '\0') {
    errln_end++;
  }

  size_t errln_len = errln_end - errln_start;

  String err_msg = z3_strcpy (yaml_error_messages[error.kind]);
  String ferr_msg = z3_interp (&err_msg, parser_filler, &error);

  eprintf ("YamlError::%s\n", yaml_error_to_string (error.kind));
  if (errln_len < 1 || errln_len > LINE_BUFFER_MAX_LENGTH) {
    eprintf (
      "anvil.yaml:%zu:%zu -> %s\n", tokenizer->line, error.pos - errln_start, ferr_msg.chr
    );
    goto drop_all_return;
  }
  char line_buffer[LINE_BUFFER_MAX_LENGTH];

  // find previous line start
  size_t prevln_start = errln_start - 1;
  while (prevln_start > 0 && input[prevln_start - 1] != '\n') {
    prevln_start--;
  }
  size_t prevln_len = errln_start - prevln_start - 1;

  if (prevln_len > 0 && prevln_len < LINE_BUFFER_MAX_LENGTH) {
    COPY_LINE (line_buffer, prevln_start, prevln_len);
    eprintf ("%3zu |%s\n", tokenizer->line - 1, line_buffer);
  }

  // error line
  COPY_LINE (line_buffer, errln_start, errln_len)
  eprintf ("%3zu |%s\n", tokenizer->line, line_buffer);

  size_t column = error.pos - errln_start;
  memset (line_buffer, ' ', column);
  memset (line_buffer + column, '^', error.len);
  line_buffer[error.len + column] = '\0';
  eprintf ("    |%s\n", line_buffer);

  // Extract the next line
  size_t nextln_start = errln_end + 1;
  size_t nextln_end = nextln_start;
  while (input[nextln_end] != '\n' && input[nextln_end] != '\0') {
    nextln_end++;
  }

  size_t nextln_len = nextln_end - nextln_start;
  if (nextln_len > 0 && nextln_len < LINE_BUFFER_MAX_LENGTH) {
    COPY_LINE (line_buffer, nextln_start, nextln_len);
    eprintf ("%3zu |%s\n", tokenizer->line + 1, line_buffer);
  }

  eprintf ("\n%s\n", ferr_msg.chr);

drop_all_return:
  z3_drops (&err_msg);
  z3_drops (&ferr_msg);
  fflush (stderr);  // NOLINT (cert-err33-c)
  _exit (EXIT_FAILURE);
}
