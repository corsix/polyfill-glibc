// If this looks like overkill for what polyfill-glibc needs, it almost
// certainly is. The heart of it was ripped out of a C preprocessor and
// only minimally modified.

#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include "tokenise.h"
#include "common.h"

void tokeniser_init(tokeniser_t* self) {
  memset(self, 0, sizeof(*self));
  sht_init(&self->idents, 4);
  self->line_starts_capacity = 8;
  self->line_starts = (uint32_t*)malloc(8 * sizeof(uint32_t));
  self->tokens_capacity = 8;
  self->tokens = (token_t*)malloc(8 * sizeof(token_t));
}

void tokeniser_free(tokeniser_t* self) {
  sht_free(&self->idents);
  free(self->tokens);
  free(self->text);
  free(self->line_starts);
  free(self->tmp);
}

void tokeniser_load_file(tokeniser_t* self, const char* path) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    FATAL("Could not open %s for reading", path);
  }
  struct stat s;
  if (fstat(fd, &s) != 0 || s.st_size < 128) {
    s.st_size = 128;
  }
  if (self->text_capacity <= (uint32_t)s.st_size) {
    free(self->text);
    self->text = malloc((self->text_capacity = (uint32_t)s.st_size + 1));
  }
  uint32_t size = 0;
  for (;;) {
    if (size >= self->text_capacity) {
      uint32_t new_capacity = self->text_capacity + (self->text_capacity >> 1);
      if (new_capacity < self->text_capacity) {
        new_capacity = self->text_capacity + 8;
      }
      self->text = realloc(self->text, new_capacity);
      self->text_capacity = new_capacity;
    }
    ssize_t n = read(fd, self->text + size, self->text_capacity - size);
    if (n > 0) {
      size += (size_t)n;
    } else if (n == 0) {
      break;
    } else if (errno != EINTR) {
      FATAL("Error reading from %s", path);
    }
  }
  self->text_size = size;
  self->filename = path;
  close(fd);
}

static uint32_t offset_to_line(tokeniser_t* self, uint32_t offset) {
  const uint32_t* starts = self->line_starts;
  uint32_t cached_result = self->line_lookup_cache;
  if (offset >= starts[cached_result]) {
    ++starts;
    if (offset < starts[cached_result]) {
      return cached_result;
    }
    if (offset >= 0xffffffffu) {
      return self->line_starts_size - 1;
    }
    do {
      ++cached_result;
    } while (offset >= starts[cached_result]);
  } else {
    do {
      --cached_result;
    } while (offset < starts[cached_result]);
  }
  self->line_lookup_cache = cached_result;
  return cached_result;
}

void tokeniser_error(tokeniser_t* self, token_t* where, const char* what) {
  uint32_t line = offset_to_line(self, where->start);
  printf("%s:%u:%u: error: %s\n", self->filename, line + 1, where->start - self->line_starts[line] + 1u, what);
  exit(1);
}

uint32_t tokeniser_next_line_start(tokeniser_t* self, uint32_t ofs) {
  uint32_t line = offset_to_line(self, ofs);
  return *(self->line_starts + line + 1);
}

static uint8_t char_flags[256] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
  1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 3, 1, 31, 31, 31, 
  31, 31, 31, 31, 31, 31, 31, 1, 1, 1, 1, 1, 1, 1, 31, 31, 31, 31, 63, 31, 15, 
  15, 15, 15, 15, 15, 15, 15, 15, 47, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 1, 
  0, 1, 1, 15, 1, 31, 31, 31, 31, 63, 31, 15, 15, 15, 15, 15, 15, 15, 15, 15, 47, 
  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 1, 1, 1, 1, 1, 7, 7, 7, 7, 7, 7, 7, 7, 
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 
  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
};
#define CF_eEpP 0x20 // e E p P
#define CF_HEX_DIGIT 0x10 // 0-9 a-f A-F
#define CF_DIGIT_OR_NON_DIGIT 0x08 // 0-9 a-z A-Z _
#define CF_IDENT_CONTINUE 0x04 // digit_or_non-digit or XID_Continue
#define CF_PP_NUMBER 0x02 // identifier_continue or .
#define CF_STR_BODY 0x01 // not ' " \\ \r \n

void tokeniser_run(tokeniser_t* self) {
  // (Re)initialise line_starts.
  self->line_starts[0] = 0;
  self->line_starts_size = 1;
  self->line_lookup_cache = 0;
  // Ensure that there is "\n\0" beyond the end of input.
  if (self->text_size + 1 >= self->text_capacity) {
    uint32_t new_capacity = self->text_capacity + (self->text_capacity >> 1);
    if (new_capacity < self->text_capacity) {
      new_capacity = self->text_capacity + 8;
    }
    self->text = realloc(self->text, new_capacity);
    self->text_capacity = new_capacity;
  }
  char* base = self->text;
  const char* src = base;
  base[self->text_size] = '\n';
  (base + 1)[self->text_size] = '\0';
  // Main loop.
  token_t* dst = self->tokens;
  token_t* end = dst + self->tokens_capacity;
  for (;;) {
    if (dst >= end) {
      uint32_t idx = dst - self->tokens;
      uint32_t new_capacity = self->tokens_capacity + (self->tokens_capacity >> 1);
      if (new_capacity < self->tokens_capacity) {
        new_capacity = self->tokens_capacity + 8;
      }
      self->tokens = (token_t*)realloc(self->tokens, new_capacity * sizeof(token_t));
      dst = self->tokens;
      end = dst + new_capacity;
      dst += idx;
      self->tokens_capacity = new_capacity;
    }
  again:
    dst->start = (uint32_t)(src - base);
    char c;
    uint32_t type;
    switch ((c = *src++)) {
    case '_': case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
    case 'g': case 'h': case 'i': case 'j': case 'k': case 'l': case 'm':
    case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't':
    case 'u': case 'v': case 'w': case 'x': case 'y': case 'z': case 'A':
    case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H':
    case 'I': case 'J': case 'K': case 'L': case 'M': case 'N': case 'O':
    case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V':
    case 'W': case 'X': case 'Y': case 'Z': {
      do {
        c = *src++;
      } while (char_flags[(uint8_t)c] & CF_IDENT_CONTINUE);
      --src;
      type = sht_intern_u(&self->idents, base + dst->start, (src - base) - dst->start);
      break; }
    case '\'': case '"': {
      type = TOK_STR_LIT + (c & 1) * 2;
      _Static_assert(TOK_STR_LIT + ('"'  & 1) * 2 == TOK_STR_LIT,  "Can use this bitwise trick");
      _Static_assert(TOK_STR_LIT + ('\'' & 1) * 2 == TOK_CHAR_LIT, "Can use this bitwise trick");
      char c0 = c;
      for (;;) {
        c = *src++;
        if (char_flags[(uint8_t)c] & CF_STR_BODY) continue;
        if (c == c0) break;
        type |= 1; /* TOK_STR_LIT -> TOK_STR_LIT_ESC, TOK_CHAR_LIT -> TOK_CHAR_LIT_ESC. */
        _Static_assert((TOK_STR_LIT     | 1) == TOK_STR_LIT_ESC,  "Can use this bitwise trick");
        _Static_assert((TOK_CHAR_LIT    | 1) == TOK_CHAR_LIT_ESC, "Can use this bitwise trick");
        if (c == '\\') c = *src++;
        if (c == '\r' || c == '\n') { --src; break; }
      }
      break; }
    case '.': { /* . or pp-number */
      c = *src;
      type = TOK_PUNCT('.');
      if ('0' <= c && c <= '9') {
        ++src;
        goto pp_number;
      }
      break; }
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9': pp_number:
      for (;;) {
        c = *src++;
        if (char_flags[(uint8_t)c] & CF_PP_NUMBER) continue;
        if ((c == '+' || c == '-') && (char_flags[(uint8_t)src[-2]] & CF_eEpP)) continue;
        break;
      }
      --src;
      type = TOK_NUMBER;
      break;
#define SIMPLE_PUNCT(c_) case c_: type = TOK_PUNCT(c_); break;
    SIMPLE_PUNCT('(')
    SIMPLE_PUNCT(')')
    SIMPLE_PUNCT('[')
    SIMPLE_PUNCT(']')
    SIMPLE_PUNCT('{')
    SIMPLE_PUNCT('}')
    SIMPLE_PUNCT(',')
    SIMPLE_PUNCT(';')
    SIMPLE_PUNCT('~')
    SIMPLE_PUNCT('-')
    SIMPLE_PUNCT('+')
    SIMPLE_PUNCT('*')
    SIMPLE_PUNCT('^')
    SIMPLE_PUNCT('%')
    SIMPLE_PUNCT('@')
    SIMPLE_PUNCT('#')
#undef SIMPLE_PUNCT
#define PUNCT_SINGLE_FLAG(c_, possible) \
    case c_: \
      type = TOK_PUNCT(c_); \
      c = *src; \
      if (((possible) & PUNCT_REPEAT) && (c == c_)) { ++src; type += PUNCT_REPEAT; } \
      else if (((possible) & PUNCT_EQ) && (c == '=')) { ++src; type += PUNCT_EQ; } \
      break;
    PUNCT_SINGLE_FLAG('&', PUNCT_REPEAT)
    PUNCT_SINGLE_FLAG('|', PUNCT_REPEAT)
    PUNCT_SINGLE_FLAG('=', PUNCT_EQ)
    PUNCT_SINGLE_FLAG('!', PUNCT_EQ)
    PUNCT_SINGLE_FLAG('<', PUNCT_REPEAT)
    PUNCT_SINGLE_FLAG('>', PUNCT_REPEAT)
    PUNCT_SINGLE_FLAG(':', PUNCT_REPEAT)
#undef PUNCT_SINGLE_FLAG
    case '/': /* / or // */
      c = *src;
      if (c == '/') {
        for (++src;;) {
          c = *src++;
          if (c == '\n') goto char_n;
          if (c == '\r') goto char_r;
        }
      }
      type = TOK_PUNCT('/');
      break;
    case '\r': char_r:
      if (*src == '\n') ++src;
      // fallthrough
    case '\n': char_n:
      if (self->line_starts_size >= self->line_starts_capacity) {
        uint32_t new_capacity = self->line_starts_capacity + (self->line_starts_capacity >> 1);
        if (new_capacity < self->line_starts_capacity) {
          new_capacity = self->line_starts_capacity + 8;
        }
        self->line_starts_capacity = new_capacity;
        self->line_starts = (uint32_t*)realloc(self->line_starts, new_capacity * sizeof(uint32_t));
      }
      self->line_starts[self->line_starts_size++] = (uint32_t)(src - base);
      goto again;
    case ' ': case '\t': case '\f': case '\v':
      while (*src == c) ++src;
      goto again;
    case '\0':
      if (src > base + self->text_size) {
        dst->type = TOK_EOF;
        dst->end = dst->start;
        self->tokens_size = dst - self->tokens;
        self->line_starts[--self->line_starts_size] = 0xffffffff;
        return;
      } else {
        type = TOK_OTHER_CHAR;
        break;
      }
    default:
      while ((c = *src), (0x80u <= (uint8_t)c) && ((uint8_t)c < 0xc0u)) ++src;
      type = TOK_OTHER_CHAR;
      break;
    }
    dst->type = type;
    dst->end = (uint32_t)(src - base);
    ++dst;
  }
}

uint32_t tokeniser_is_hex_range(tokeniser_t* self, uint32_t start, uint32_t end) {
  const char* text = self->text;
  uint32_t result = CF_HEX_DIGIT;
  while (start < end) {
    result &= char_flags[(uint8_t)text[start++]];
  }
  return result;
}

uint64_t tokeniser_decode_hex_range(tokeniser_t* self, uint32_t start, uint32_t end) {
  uint64_t uval = 0;
  const char* text = self->text;
  while (start < end) {
    char c = text[start++];
    c += (c & 16) ? 16 : 9;
    uval = (uval << 4) + (c & 15u);
  }
  return uval;
}

static char* acquire_tmp(tokeniser_t* self, uint32_t n) {
  if (n > self->tmp_capacity) {
    n = self->tmp_capacity + (n < self->tmp_capacity ? self->tmp_capacity : n);
    free(self->tmp);
    self->tmp = malloc(n);
    self->tmp_capacity = n;
  }
  return self->tmp;
}

const char* tokeniser_decode_str_lit(tokeniser_t* self, token_t* srct, uint32_t* length) {
  if (srct->type == TOK_STR_LIT || srct->type == TOK_CHAR_LIT) {
    if (length) *length = srct->end - srct->start - 2;
    return self->text + srct->start + 1;
  } else if (srct->type != TOK_STR_LIT_ESC && srct->type != TOK_CHAR_LIT_ESC) {
    return NULL;
  }
  char* result = acquire_tmp(self, srct->end - srct->start);
  char* out = result;
  const char* in = self->text + srct->start + 1;
  const char* end = self->text + srct->end - 1;
  while (in < end) {
    uint8_t c = (uint8_t)*in++;
    if (c != (uint8_t)'\\') {
      *out++ = c;
      continue;
    }
    switch (c = *in++) {
    case 'a': *out++ = '\a'; continue;
    case 'b': *out++ = '\b'; continue;
    case 'f': *out++ = '\f'; continue;
    case 'n': *out++ = '\n'; continue;
    case 'r': *out++ = '\r'; continue;
    case 't': *out++ = '\t'; continue;
    case 'v': *out++ = '\v'; continue;
    case 'x':
      c = *in;
      if (char_flags[c] & CF_HEX_DIGIT) {
        c += (c & 16) ? 16 : 9;
        uint32_t hval = (c & 15u);
        while (char_flags[c = (uint8_t)*++in] & CF_HEX_DIGIT) {
          c += (c & 16) ? 16 : 9;
          hval = (hval << 4) + (c & 15u);
        }
        *out++ = hval;
        continue;
      } else {
        *out++ = 'x';
        continue;
      }
    case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7': {
      uint32_t oval = c - '0';
      c = *in;
      if ('0' <= c && c <= '7') {
        oval = (oval * 8) + (c - '0');
        c = *++in;
        if ('0' <= c && c <= '7') {
          oval = (oval * 8) + (c - '0');
          ++in;
        }
      }
      *out++ = oval;
      continue; }
    default:
      *out++ = c;
      continue;
    }
  }
  if (length) *length = (out - result);
  *out++ = '\0';
  return result;
}

uint64_t tokeniser_decode_char_lit(tokeniser_t* self, token_t* srct) {
  uint32_t len;
  const char* src = tokeniser_decode_str_lit(self, srct, &len);
  const char* end = src + len;
  uint64_t uval = 0;
  while (src < end) {
    char c = *src++;
    uval = (uval << 8) | (uint8_t)c;
  }
  return uval;
}

uint32_t tokeniser_decode_uint(tokeniser_t* self, token_t* srct, uint64_t* dst) {
  const char* src = self->text + srct->start;
  char c = *src++;
  if (c == '.') return 0;
  uint64_t uval = 0;
  if (c == '0') {
    c = *src++;
    if (c == 'x' || c == 'X') { /* Hex. */
      for (;;) {
        c = *src++;
        if (char_flags[(uint8_t)c] & CF_HEX_DIGIT) {
          c += (c & 16) ? 16 : 9;
          uval = (uval << 4) + (c & 15u);
        } else if (!(c == '\'' && (char_flags[(uint8_t)*src] & CF_HEX_DIGIT))) {
          break;
        }
      }
    } else if (c == 'b' || c == 'B') { /* Binary. */
      for (;;) {
        c = *src++;
        if ('0' <= c && c <= '1') {
          uval = uval * 2 + (c - '0');
        } else if (!(c == '\'' && '0' <= *src && *src <= '1')) {
          break;
        }
      }
    } else { /* Octal. */
      for (;;) {
        if ('0' <= c && c <= '7') {
          uval = uval * 8 + (c - '0');
        } else if (!(c == '\'' && '0' <= *src && *src <= '7')) {
          break;
        }
        c = *src++;
      }
    }
  } else { /* Decimal. */
    uval = (c - '0');
    for (;;) {
      c = *src++;
      if ('0' <= c && c <= '9') {
        uval = uval * 10 + (c - '0');
      } else if (!(c == '\'' && '0' <= *src && *src <= '9')) {
        break;
      }
    }
  }
  --src;
  *dst = uval;
  return src == self->text + srct->end;
}
