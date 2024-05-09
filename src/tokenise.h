#pragma once
#include "sht.h"

typedef struct token_t {
  uint32_t type; // TOK_
  uint32_t start;
  uint32_t end;
} token_t;

// Idents are all less than this (and are indices into tokeniser_t::idents).
#define TOK_IDENT_THR 0xffffff80u

// Single-character punctuation c is TOK_PUNCT('c').
// Where recognised, cc is TOK_PUNCT('c') + PUNCT_REPEAT,
// c= is TOK_PUNCT('c') + PUNCT_EQ, and
// cc= is TOK_PUNCT('c') + PUNCT_REPEAT + PUNCT_EQ.
#define TOK_PUNCT(c) \
  ((((c) + ((c) < 0x41 ? 0 : (c) < 0x61 ? 21 : 27)) & 31u) + TOK_IDENT_THR)
#define PUNCT_REPEAT 32u
#define PUNCT_EQ 64u

// Other tokens occupy unused PUNCT_REPEAT + PUNCT_EQ encodings.
#define TOK_NUMBER         0xfffffff7u // TOK_PUNCT('~') + PUNCT_REPEAT + PUNCT_EQ
#define TOK_STR_LIT        0xfffffff8u // TOK_PUNCT('}') + PUNCT_REPEAT + PUNCT_EQ
#define TOK_STR_LIT_ESC    0xfffffff9u // TOK_PUNCT('|') + PUNCT_REPEAT + PUNCT_EQ
#define TOK_CHAR_LIT       0xfffffffau // TOK_PUNCT('{') + PUNCT_REPEAT + PUNCT_EQ
#define TOK_CHAR_LIT_ESC   0xfffffffbu // TOK_PUNCT('`') + PUNCT_REPEAT + PUNCT_EQ
#define TOK_OTHER_CHAR     0xfffffffdu // TOK_PUNCT('=') + PUNCT_REPEAT + PUNCT_EQ
#define TOK_EOF            0xffffffffu // TOK_PUNCT('?') + PUNCT_REPEAT + PUNCT_EQ

typedef struct tokeniser_t {
  sht_t idents;
  token_t* tokens;
  uint32_t tokens_size;
  uint32_t tokens_capacity;
  char* text;
  uint32_t text_size;
  uint32_t text_capacity;
  uint32_t* line_starts;
  uint32_t line_starts_size;
  uint32_t line_starts_capacity;
  uint32_t line_lookup_cache;
  uint32_t tmp_capacity;
  char* tmp;
  const char* filename; // For errors.
} tokeniser_t;

void tokeniser_init(tokeniser_t* self);
void tokeniser_load_file(tokeniser_t* self, const char* path);
void tokeniser_run(tokeniser_t* self); // Reads from text, (re)populates other arrays.
void tokeniser_free(tokeniser_t* self);
void tokeniser_error(tokeniser_t* self, token_t* where, const char* what);
uint32_t tokeniser_next_line_start(tokeniser_t* self, uint32_t ofs);
uint32_t tokeniser_is_hex_range(tokeniser_t* self, uint32_t start, uint32_t end);
uint64_t tokeniser_decode_hex_range(tokeniser_t* self, uint32_t start, uint32_t end);
uint32_t tokeniser_decode_uint(tokeniser_t* self, token_t* src, uint64_t* dst);
uint64_t tokeniser_decode_char_lit(tokeniser_t* self, token_t* src);
const char* tokeniser_decode_str_lit(tokeniser_t* self, token_t* srct, uint32_t* length);
