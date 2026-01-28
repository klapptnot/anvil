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
#include <z3_vector.h>

#define LINE_BUFFER_CLAMP(x) ((x) > MAX_ERROR_LINE_LENGTH ? MAX_ERROR_LINE_LENGTH : (x))
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
    case NUMBER_TOO_LONG:
      return "NUMBER_TOO_LONG";
    case KEY_TOO_LONG:
      return "KEY_TOO_LONG";
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

[[noreturn]] [[maybe_unused]]
static void parser_error (YamlParser* yp, YamlError error) {
  // Error messages corresponding to YamlError enum
  const char* yaml_error_messages[] = {
    "Tabs cannot be used for indentation.",
    "Expected #{exp}, found #{got}.",
    "Unexpected character.",
    "#{} is redefined in the current context.",
    "Alias *#{} is undefined.",
    "Anchor &#{} is already defined.",
    "Missing value after key #{}.",
    "Comma missing between elements in a collection.",
    "Reached #{got} while looking for matching `#{exp}` quote.",
    "Number is over 64 chars, not counting underscores or leading zeros",
    "Key exceeds length limit, may not surpass 255 chars"
  };

  String* filename = z3_get(yp->store->strs, 0);

  ScopedString err_msg = z3_strcpy (yaml_error_messages[error.kind]);
  ScopedString ferr_msg = z3_interp (&err_msg, parser_filler, &error);

  eprintf ("YamlError::%s\n", yaml_error_to_string (error.kind));
  eprintf ("%s:%hu:%hu -> %s\n", filename->chr, yp->line, yp->lpos, ferr_msg.chr);

  fflush (stderr);  // NOLINT (cert-err33-c)
  _exit (EXIT_FAILURE);
}
