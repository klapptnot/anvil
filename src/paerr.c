#define __YAML_DISPLAY

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <yaml.h>
#include <z3_toys.h>
#include <z3_string.h>

const char* yaml_error_to_string (YamlErrorKind error) {
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

bool parser_filler (String* res, void* ctx, char* item, size_t len) {
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
      z3_pushl (res, ctxs->got, strlen (ctxs->got));
      return true;
    case KEY_REDEFINITION:                            // To be improved later on
      z3_pushl (res, ctxs->got, strlen (ctxs->got));  // key name
      return true;
    case UNDEFINED_ALIAS:
      z3_pushl (res, ctxs->got, strlen (ctxs->got));  // alias name
      return true;
    case REDEFINED_ALIAS:
      z3_pushl (res, ctxs->got, strlen (ctxs->got));  // alias name
      return true;
    case MISSING_VALUE:
      z3_pushl (res, ctxs->got, strlen (ctxs->got));
      return true;
    case MISSING_COMMA:
      return true;
    default:
      z3_pushl (res, "Unknown error occurred.", 23);
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
      "Reached EOF while looking for matching quote."
  };
  // Find the start of the error line
  const char* input = tokenizer->input;
  size_t line_start = error.pos;
  while (line_start > 0 && input[line_start - 1] != '\n') {
    line_start--;
  }

  // Find the end of the error line
  size_t line_end = error.pos;
  while (input[line_end] != '\n' && input[line_end] != '\0') {
    line_end++;
  }

  // Extract the error line
  size_t line_length = line_end - line_start;
  char line_buffer[256];
  if (line_length >= sizeof (line_buffer)) {
    line_length = sizeof (line_buffer) - 1;
  }
  strncpy (line_buffer, input + line_start, line_length);
  line_buffer[line_length] = '\0';

  // Extract the previous line
  size_t prev_start = line_start - 1;
  while (prev_start > 0 && input[prev_start - 1] != '\n') {
    prev_start--;
  }
  size_t prev_length = line_start - prev_start - 1;
  char prev_buffer[256] = "";
  if (prev_length > 0) {
    strncpy (prev_buffer, input + prev_start, prev_length);
    prev_buffer[prev_length] = '\0';
  }

  // Extract the next line
  size_t next_start = line_end + 1;
  size_t next_end = next_start;
  while (input[next_end] != '\n' && input[next_end] != '\0') {
    next_end++;
  }
  size_t next_length = next_end - next_start;
  char next_buffer[256] = "";
  if (next_length > 0) {
    strncpy (next_buffer, input + next_start, next_length);
    next_buffer[next_length] = '\0';
  }

  int column = (int)(error.pos - line_start);

  fprintf (stderr, "YamlError::%s\n", yaml_error_to_string (error.kind));

  if (prev_length > 0) fprintf (stderr, "%3zu |%s\n", tokenizer->line - 1, prev_buffer);

  fprintf (stderr, "%3zu |%s\n", tokenizer->line, line_buffer);

  char* carets = malloc (error.len + column + 2);
  memset (carets, ' ', column);
  memset (carets + column, '^', error.len);
  carets[error.len + column] = '\n';
  carets[error.len + column + 1] = '\0';

  String err_msg = z3_strcpy (yaml_error_messages[error.kind]);
  String ferr_msg = z3_interp (&err_msg, parser_filler, &error);

  IGNORE_UNUSED (write (STDERR_FILENO, "    |", 5));
  IGNORE_UNUSED (write (STDERR_FILENO, carets, error.len + column + 1));

  if (next_length > 0) fprintf (stderr, "%3zu |%s\n", tokenizer->line + 1, next_buffer);
  fprintf (stderr, "\n%s\n", ferr_msg.chr);

  z3_drops (&err_msg);
  free (carets);
  z3_drops (&ferr_msg);
  exit (EXIT_FAILURE);
}
