// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2025-present Klapptnot

/// @file z3_string.h
/// @brief Heap-allocated string management with automatic memory handling.
///
/// Provides growable heap-allocated strings, string interpolation, and
/// escape/unescape utilities, plus scoped resource cleanup for string memory.
///
/// @note Requires C23 (`-std=c23`) and `z3_toys.h`.
#pragma once

#include <notrust.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <z3_toys.h>

/// @brief Maximum length in bytes of a single UTF-8 sequence (4 for standard UTF-8).
#define Z3_UTF8_MAX_SEQ_LEN  4

/// @brief Buffer size needed to hold one UTF-8 character plus a null terminator.
#define Z3_UTF8_CHAR_BUF_LEN 5

/// Heap-allocated growable string
typedef struct {
  ustr chr;   ///< Pointer to the character array
  usize len;  ///< Current length (excluding null terminator)
  usize max;  ///< Maximum capacity
} String;

/// Heap-allocated string slice
typedef struct {
  cstr chr;   ///< Pointer to the slice's byte data. Not null-terminated.
  usize len;  ///< String slice length
} StringSlice;

/// @brief Print debug information about a string
///
/// Prints formatted debug information including length, max capacity,
/// and the character payload of the given string `s`.
///
/// @param s The `String` (by value, e.g. `myStr` not `&myStr`) to print.
#define z3_str_dbg(s)                                                                       \
  printf (                                                                                  \
    #s " = String {\n  len: %zu,\n  max: %zu,\n  chr: '%s'\n}\n", (s).len, (s).max, (s).chr \
  );

/// @brief Push a string literal onto a String, using `sizeof` to get its length.
/// @param str Pointer to the target `String`
/// @param lit A string literal (e.g. `"hello"`) — must be an actual literal.
#define z3_pushlit(str, lit) z3_pushl (str, lit, sizeof (lit) - 1)

/// @brief Construct a `StringSlice` from a raw pointer and length.
/// @param str A `cstr` pointer to the start of the slice data.
/// @param length Number of bytes the slice covers.
/// @return A `StringSlice` literal wrapping the given pointer and length.
#define z3_string_slice(str, length) (StringSlice) {.chr = (str), .len = (length)}

/// @brief Create a new empty String, with at least `min` capacity
/// @param min Minimum capacity to preallocate
/// @return A new initialized `String`.
String z3_str (usize min);

/// @brief Create a new String from a C-style string
/// @param s Pointer to the null-terminated C-string
/// @return A newly allocated `String` containing the copied data.
String z3_strcpy (cstr s);

/// @brief Create a duplicate of an existing String
/// @param str Pointer to the `String` to copy
/// @return A new `String` containing the duplicated content.
String z3_strdup (const String* str);

/// @brief Append a single byte (character) to a String
/// @param str Pointer to the target `String`
/// @param c The single `u8` character to append
void z3_pushc (String* str, u8 c);

/// @brief Append a raw byte slice (string) to a String
/// @param str Pointer to the target `String`
/// @param s Pointer to the byte buffer to append. Need not be null-terminated.
/// @param l Number of bytes to append
void z3_pushl (String* str, nstr s, usize l);

/// @brief Ensure String has enough allocated memory
/// @param str Pointer to the `String`
/// @param additional Number of additional bytes required
void z3_reserve (String* str, usize additional);

/// @brief Free the memory used by a String
/// @param str Pointer to the `String` to clean up
void z3_drops (String* str);

/// @brief Escape a string, converting control characters to escape sequences
/// @param input Pointer to the raw input string buffer
/// @param len Length of the input buffer
/// @return A new escaped `String`.
String z3_escape (cstr input, usize len);

/// @brief Unescape a string, converting escape sequences to their respective characters
/// @param input Pointer to the escaped input string buffer
/// @param len Length of the input buffer
/// @return A new unescaped `String`.
String z3_unescape (cstr input, usize len);

/// @brief Signature for a template placeholder resolver.
///
/// Called by z3_interp() once for each `#{...}` placeholder found in the
/// template. Receives the string built up so far, the raw placeholder
/// contents (without the `#{` `}` delimiters), and the user-supplied context.
///
/// @param out  The `String` being accumulated so far, to push values in.
/// @param expr Slice into the *original* template string holding the raw
///             placeholder text. This is a view, not an owned copy — do
///             not mutate or free it.
/// @param ctx  Opaque context pointer, forwarded unchanged from
///             z3_interp()'s @p ctx argument.
/// @return `true` to keep the placeholder's `#{...}` text appended to
///         @p out, `false` to skip it.
typedef bool (*z3_filler_fn)(String* out, const StringSlice* expr, void* ctx);

/// @brief Interpolate a template string with values from a filler function.
///
/// Scans @p tmplt for `#{...}` placeholders and, for each one, invokes @p
/// filler with the string accumulated so far and a slice covering that
/// placeholder's contents, and user-supplied context.
///
/// @param tmplt  Pointer to the template `String` to interpolate.
/// @param filler Callback invoked once per placeholder; see ::z3_filler_fn.
/// @param ctx    User-defined context, passed through unchanged to @p filler.
///
/// @return A newly allocated, interpolated `String`.
String z3_interp(const String* tmplt, z3_filler_fn filler, void* ctx);

/// @brief Validate if string contains valid UTF-8 (structure + value)
///
/// Rejects: truncated sequences, bad continuation bytes, overlong
/// encodings, encoded surrogates (`0xD800-0xDFFF`), and codepoints
/// beyond the max valid value (`0x10FFFF`).
///
/// @param s Pointer to the `String` to validate
/// @return `true` if valid UTF-8, otherwise `false`
bool z3_utf8_valid (const String* s);

/// @brief Retrieve a UTF-8 character slice at a specific codepoint index
/// @param s Pointer to the `String`
/// @param idx 0-indexed codepoint position
/// @return A `StringSlice` pointing into @p s. This is a view, not an
///         owned copy — do not free it.
StringSlice z3_utf8_char (const String* s, u64 idx);

/// @brief Count UTF-8 codepoints (characters) in a String
/// @param s Pointer to the `String`
/// @return Total number of UTF-8 characters
u64 z3_utf8_len (const String* s);

/// @brief Get byte index of the Nth UTF-8 codepoint (0-indexed)
/// @param s Pointer to the `String`
/// @param codepoint_n The target codepoint index
/// @return The byte offset into the string
u64 z3_utf8_index (const String* s, u64 codepoint_n);

/// @brief Decode a UTF-8 sequence into its codepoint value
///
/// Caller must have already validated `seq_len` bytes are available and
/// that continuation bytes match the `10xxxxxx` pattern.
///
/// @param c Pointer to the start of the UTF-8 sequence bytes
/// @param seq_len Length of the sequence
/// @return The decoded 32-bit codepoint value
u32 z3_utf8_decode (const u8* c, u64 seq_len);

/// @brief Copy character at index `idx` into `buf`
/// @param s Pointer to the `String`
/// @param idx Target codepoint index
/// @param buf Destination buffer, must be at least `Z3_UTF8_CHAR_BUF_LEN` bytes
/// @return A pointer to the resulting UTF-8 character string (i.e. @p buf)
const u8* z3_utf8_char_str (const String* s, u64 idx, u8 buf[Z3_UTF8_CHAR_BUF_LEN]);

/// @brief Get UTF-8 sequence length from first byte
/// @param c The first byte of a UTF-8 sequence
/// @return The expected sequence length in bytes
u64 z3_utf8_seqlen (u8 c);

/// @brief Define a String with automatic cleanup via GCC/Clang attribute
#define ScopedString [[gnu::cleanup (z3_drops)]] String

#ifdef Z3_STRING_IMPL
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void z3_reserve (String* str, usize additional) {
  if (str == nullptr) die ("z3_reserve: String* == nullptr");

  if (str->len + additional >= str->max) {
    usize new_max = powtwo_ceil (str->len + additional + 1);

    ustr new_chr = calloc (new_max, sizeof (c8));
    if (new_chr == nullptr) die ("z3_reserve: requested %zu bytes", new_max);

    if (str->chr != nullptr) {
      memcpy (new_chr, str->chr, str->len);
      free (str->chr);
    }

    str->chr = new_chr;
    str->max = new_max;
  }
}

void z3_pushc (String* str, u8 c) {
  z3_reserve (str, 1);
  str->chr[str->len] = c;
  str->len++;
  str->chr[str->len] = '\0';
}

void z3_pushl (String* str, nstr s, usize l) {
  z3_reserve (str, l);
  memcpy (str->chr + str->len, s, l);
  str->len += l;
  str->chr[str->len] = '\0';
}

String z3_str (usize min) {
  String str = {0};
  str.max = powtwo_ceil (min);  // Initial capacity
  str.len = 0;
  str.chr = calloc (str.max, sizeof (c8));
  if (str.chr == nullptr) die ("z3_str: requested %zu bytes", str.max);
  return str;
}

String z3_strcpy (cstr s) {
  String str = {0};
  usize len = strlen ((nstr)s) + 1;
  str.max = ((len & (len - 1)) == 0) ? len : powtwo_ceil (len);
  str.len = len - 1;
  str.chr = calloc (str.max, sizeof (c8));
  if (str.chr == nullptr) die ("z3_strcpy: requested %zu bytes", str.max);
  memcpy (str.chr, s, len - 1);
  str.chr[str.len] = '\0';
  return str;
}

String z3_strdup (const String* str) {
  String s = {0};
  if (str->len == 0) return s;

  s.max = str->max;
  s.len = str->len;
  s.chr = calloc (s.max, sizeof (c8));
  if (s.chr == nullptr) die ("z3_strdup: requested %zu bytes", s.max);

  memcpy (s.chr, str->chr, str->len);
  s.chr[s.len] = '\0';

  return s;
}

void z3_drops (String* str) {
  if (!str || !str->chr) return;

  free (str->chr);
  str->chr = nullptr;
  str->len = 0;
  str->max = 0;
}

String z3_interp (const String* tmplt, z3_filler_fn filler, void* ctx) {
  String result = z3_str (32);  // NOLINT(readability-magic-numbers)

  usize i = 0;

  while (i < tmplt->len) {
    if (tmplt->chr[i] == '\\') {
      z3_pushc (&result, (i++, tmplt->chr[i++]));
      continue;
    }

    if (!(i + 1 < tmplt->len && tmplt->chr[i] == '#' && tmplt->chr[i + 1] == '{')) {
      z3_pushc (&result, tmplt->chr[i]);
      i++;
      continue;
    }

    usize path_start = i + 2;  // Skip "#{"
    usize path_end = path_start;

    // Find the closing '}'
    while (path_end < tmplt->len && tmplt->chr[path_end] != '}') {
      if (!(isalnum (tmplt->chr[path_end]) || tmplt->chr[path_end] == '_' ||
            tmplt->chr[path_end] == '-'))
        break;
      path_end++;
    }
    if (tmplt->chr[path_end] != '}' || path_end >= tmplt->len) {
      // No closing '}' found, treat as literal text
      usize path_len = path_end - path_start + 2;
      ustr path = malloc (path_len + 1);
      if (path) {
        memcpy (path, tmplt->chr + path_start - 2, path_len);
        path[path_len] = '\0';
        z3_pushl (&result, (nstr)path, path_len);
        free (path);
      }
      i += path_len;
      continue;
    }

    StringSlice path = z3_string_slice (tmplt->chr + path_start, path_end - path_start);
    if (filler (&result, &path, ctx)) {
      z3_pushl (&result, (nstr)(tmplt->chr + i), path.len + 3);  // push entire #{...}
    }

    // Move past the closing '}'
    i = path_end + 1;
  }

  return result;
}

String z3_escape (cstr input, usize len) {
  u8 hex_digits[] = "0123456789abcdef";
  String s = z3_str (len);
  usize l = 0;

  // loop until `\0`, or until length
  while (l < len && *input) {
    u8 c = *input;
    switch (c) {
      case '\a':
        z3_pushl (&s, "\\a", 2);
        break;  // Bell
      case '\b':
        z3_pushl (&s, "\\b", 2);
        break;  // Backspace
      case '\f':
        z3_pushl (&s, "\\f", 2);
        break;  // Formfeed
      case '\n':
        z3_pushl (&s, "\\n", 2);
        break;
      case '\r':
        z3_pushl (&s, "\\r", 2);
        break;
      case '\t':
        z3_pushl (&s, "\\t", 2);
        break;
      case '\v':
        z3_pushl (&s, "\\v", 2);
        break;  // Vertical tab
      case '\\':
        z3_pushl (&s, "\\\\", 2);
        break;
      case '\"':
        z3_pushl (&s, "\\\"", 2);
        break;
      case '\'':
        z3_pushl (&s, "\\\'", 2);
        break;

      // Printable ASCII (0x20 - 0x7E), no need to escape
      default:
        if (c < 0x20 || c > 0x7E) {  // NOLINT(readability-magic-numbers)
          // Non-printables escaped as hex
          z3_pushc (&s, '\\');                        // Escape char
          z3_pushc (&s, 'x');                         // 'x' for hex escape

          z3_pushc (&s, hex_digits[(c >> 4) & 0xF]);  // NOLINT(readability-magic-numbers)
          z3_pushc (&s, hex_digits[c & 0xF]);         // NOLINT(readability-magic-numbers)
        } else {
          z3_pushc (&s, c);
        }
        break;
    }
    input++;
    l++;
  }
  return s;
}

String z3_unescape (cstr input, usize len) {
  String s = z3_str (len);
  usize l = 0;

  // loop until `\0`, or until length
  while (l < len && *input) {
    if (*input == '\\') {
      input++;  // Skip the backslash

      switch (*input) {
        case 'a':
          z3_pushc (&s, '\a');
          break;
        case 'b':
          z3_pushc (&s, '\b');
          break;
        case 'f':
          z3_pushc (&s, '\f');
          break;
        case 'n':
          z3_pushc (&s, '\n');
          break;
        case 'r':
          z3_pushc (&s, '\r');
          break;
        case 't':
          z3_pushc (&s, '\t');
          break;
        case 'v':
          z3_pushc (&s, '\v');
          break;
        case '\\':
          z3_pushc (&s, '\\');
          break;
        case '\"':
          z3_pushc (&s, '\"');
          break;
        case '\'':
          z3_pushc (&s, '\'');
          break;

        case 'x': {
          input++;  // Skip 'x'
          if (!isxdigit (*input)) {
            z3_pushl (&s, "\\x", 2);
            z3_pushc (&s, *input++);
            break;
          }
          l++;
          u8 byte_value = 0;
          u8 c = *input++;

          if (c >= '0' && c <= '9')
            byte_value |= (c - '0');
          else if (c >= 'a' && c <= 'f')
            byte_value |= (c - 'a' + 10);  // NOLINT(readability-magic-numbers)
          else if (c >= 'A' && c <= 'F')
            byte_value |= (c - 'A' + 10);  // NOLINT(readability-magic-numbers)

          c = *input;
          byte_value <<= 4;

          if (c >= '0' && c <= '9')
            byte_value |= (c - '0');
          else if (c >= 'a' && c <= 'f')
            byte_value |= (c - 'a' + 10);  // NOLINT(readability-magic-numbers)
          else if (c >= 'A' && c <= 'F')
            byte_value |= (c - 'A' + 10);  // NOLINT(readability-magic-numbers)
          z3_pushc (&s, byte_value);

          break;
        }

        // In case of invalid escape, just add the backslash
        default:
          z3_pushc (&s, '\\');
          z3_pushc (&s, *input);
          break;
      }
    } else {
      z3_pushc (&s, *input);
    }
    input++;
    l++;
  }
  return s;
}

StringSlice z3_utf8_char (const String* s, u64 idx) {
  u64 byte_offset = z3_utf8_index (s, idx);
  if (byte_offset >= s->len) {
    return (StringSlice) {.chr = nullptr, .len = 0};
  }

  u64 char_len = z3_utf8_seqlen (s->chr[byte_offset]);
  return (StringSlice) {.chr = s->chr + byte_offset, .len = char_len};
}

const u8* z3_utf8_char_str (const String* s, u64 idx, u8 buf[Z3_UTF8_CHAR_BUF_LEN]) {
  u64 byte_offset = z3_utf8_index (s, idx);
  if (byte_offset >= s->len) {
    buf[0] = '\0';
    return buf;
  }

  u64 char_len = z3_utf8_seqlen (s->chr[byte_offset]);
  for (u64 i = 0; i < char_len; i++) {
    buf[i] = s->chr[byte_offset + i];
  }
  buf[char_len] = '\0';

  return buf;
}

u64 z3_utf8_seqlen (u8 c) {
  if ((c & 0x80) == 0) return 1;     // 0xxxxxxx (ASCII) NOLINT(readability-magic-numbers)
  if ((c & 0xE0) == 0xC0) return 2;  // 110xxxxx (2-byte) NOLINT(readability-magic-numbers)
  if ((c & 0xF0) == 0xE0) return 3;  // 1110xxxx (3-byte, most CJK) NOLINT(readability-magic-numbers)
  if ((c & 0xF8) == 0xF0) return 4;  // 11110xxx (4-byte, emoji) NOLINT(readability-magic-numbers)
  return 1;  // Invalid UTF-8, treat as single byte
}

u64 z3_utf8_len (const String* s) {
  if (!s || !s->chr) return 0;

  u64 count = 0;
  u64 i = 0;

  while (i < s->len) {
    u8 c = s->chr[i];
    u64 seq_len = z3_utf8_seqlen (c);
    i += seq_len;
    count++;
  }

  return count;
}

u64 z3_utf8_index (const String* s, u64 codepoint_n) {
  if (!s || !s->chr) return 0;

  u64 count = 0;
  u64 i = 0;

  while (i < s->len && count < codepoint_n) {
    u8 c = s->chr[i];
    i += z3_utf8_seqlen (c);
    count++;
  }

  return i;
}

u32 z3_utf8_decode (const u8* c, u64 seq_len) {
  switch (seq_len) {
    case 1:
      return c[0];
    case 2:
      return ((u32)(c[0] & 0x1F) << 6) | (u32)(c[1] & 0x3F); // NOLINT(readability-magic-numbers)
    case 3:
      return ((u32)(c[0] & 0x0F) << 12) | ((u32)(c[1] & 0x3F) << 6) | (u32)(c[2] & 0x3F); // NOLINT(readability-magic-numbers)
    case 4:
      return ((u32)(c[0] & 0x07) << 18) | ((u32)(c[1] & 0x3F) << 12) | // NOLINT(readability-magic-numbers)
             ((u32)(c[2] & 0x3F) << 6) | (u32)(c[3] & 0x3F); // NOLINT(readability-magic-numbers)
    default:
      return 0xFFFD;  // replacement char, shouldn't happen NOLINT(readability-magic-numbers)
  }
}

bool z3_utf8_valid (const String* s) {
  if (!s || !s->chr) return false;

  u64 i = 0;
  while (i < s->len) {
    u8 c = s->chr[i];
    u64 seq_len = z3_utf8_seqlen (c);

    // Check we have enough bytes
    if (i + seq_len > s->len) return false;

    // Validate continuation bytes
    for (u64 j = 1; j < seq_len; j++) {
      if ((s->chr[i + j] & 0xC0) != 0x80) return false; // NOLINT(readability-magic-numbers)
    }

    // Structure is fine, now check the decoded value is legit.
    // Skip for seq_len == 1: ASCII has no overlong/surrogate concept.
    if (seq_len > 1) {
      u32 cp = z3_utf8_decode ((const u8*)s->chr + i, seq_len);

      static const u32 min_cp[5] = {0, 0, 0x80, 0x800, 0x10000};
      if (cp < min_cp[seq_len]) return false;          // overlong
      if (cp >= 0xD800 && cp <= 0xDFFF) return false;  // surrogate NOLINT(readability-magic-numbers)
      if (cp > 0x10FFFF) return false;                 // out of range NOLINT(readability-magic-numbers)
    }

    i += seq_len;
  }

  return true;
}

#endif  // Z3_STRING_IMPL
