#include <stdbool.h>
#include <string.h>
#include "common.h"
#include "tokenise.h"

typedef struct lstr {
  const char* chars;
  size_t len;
} lstr;

typedef struct parsed_name_t {
  lstr lib;
  lstr name;
  lstr version;
} parsed_name_t;

typedef struct outbuf_t {
  char* bytes;
  uint32_t size;
  uint32_t capacity;
} outbuf_t;

static void populate_lstr(lstr* dst, tokeniser_t* ctx, uint32_t start, uint32_t end) {
  dst->chars = ctx->text + start;
  dst->len = end - start;
}

static token_t* parse_name(token_t* t, parsed_name_t* dst, tokeniser_t* ctx) {
  uint32_t start = t->start;
  uint32_t end = t->start;
  token_t* colon_colon = NULL;
  token_t* at = NULL;
  while (t->start == end) {
    uint32_t tt = t->type;
    if (tt == TOK_EOF) {
      break;
    } else if (tt == (TOK_PUNCT(':') + PUNCT_REPEAT)) {
      if (!colon_colon) {
        colon_colon = t;
        at = NULL;
      }
    } else if (tt == TOK_PUNCT('@')) {
      if (!at) {
        at = t;
      }
    }
    end = t->end;
    ++t;
  }
  if (colon_colon) {
    populate_lstr(&dst->lib, ctx, start, colon_colon->start);
    start = colon_colon->end;
  } else {
    dst->lib.chars = NULL;
    dst->lib.len = 0;
  }
  populate_lstr(&dst->name, ctx, start, at ? at->start : end);
  if (at) {
    populate_lstr(&dst->version, ctx, at->end, end);
  } else {
    dst->version.chars = NULL;
    dst->version.len = 0;
  }
  return t;
}

static int compare_ver_u qsort_r_comparator(const void* lhs_, const void* rhs_, void* ctx) {
  uint32_t lhs = (uint32_t)*(const uint64_t*)lhs_;
  uint32_t rhs = (uint32_t)*(const uint64_t*)rhs_;
  if (lhs == rhs) return 0;
  sht_t* sht = (sht_t*)ctx;
  const char* lhs_s = sht_u_key(sht, lhs);
  const char* rhs_s = sht_u_key(sht, rhs);
  return version_str_less(lhs_s, rhs_s) ? 1 : -1;
}

static uint32_t outbuf_emplace(outbuf_t* ob, uint32_t n) {
  uint32_t result = ob->size;
  uint32_t new_size = result + n;
  ob->size = new_size;
  if (new_size > ob->capacity) {
    uint32_t new_capacity = ob->capacity + (n >= ob->capacity ? n : ob->capacity);
    ob->bytes = realloc(ob->bytes, new_capacity);
    memset(ob->bytes + ob->capacity, 0, new_capacity - ob->capacity);
    ob->capacity = new_capacity;
  }
  return result;
}

static char* outbuf_emplace_p(outbuf_t* ob, uint32_t n) {
  n = outbuf_emplace(ob, n);
  return ob->bytes + n;
}

int main(int argc, const char** argv) {
  if (argc < 3) {
    FATAL("Usage: %s in_filename out_filename", argv[0]);
  }

  sht_t renames;
  sht_init(&renames, sizeof(sht_t));

  tokeniser_t toks;
  tokeniser_init(&toks);
  tokeniser_load_file(&toks, argv[1]);
  tokeniser_run(&toks);
  token_t* t = toks.tokens;
  while (t->type != TOK_EOF) {
    parsed_name_t old_name;
    uint32_t line_lim = tokeniser_next_line_start(&toks, t->start);
    token_t* t1 = parse_name(t, &old_name, &toks);
    if (old_name.lib.chars != NULL || old_name.version.chars == NULL) {
      tokeniser_error(&toks, t, "Malformed old_name");
    }
    sht_t* inner = (sht_t*)sht_intern_p(&renames, old_name.version.chars, old_name.version.len);
    if (!inner->count) {
      sht_init(inner, sizeof(parsed_name_t));
    }
    parsed_name_t* dst = (parsed_name_t*)sht_intern_p(inner, old_name.name.chars, old_name.name.len);
    if (dst->name.chars) {
      tokeniser_error(&toks, t, "Duplicate old_name");
    }
    if (t1->start >= line_lim) {
      tokeniser_error(&toks, t1 - 1, "Missing new_name");
    }
    t = parse_name(t1, dst, &toks);
    if (t->start < line_lim) {
      tokeniser_error(&toks, t, "Garbage after new_name");
    }
  }

  uint64_t* sorted_vers = malloc(renames.count * sizeof(uint64_t));
  for (uint32_t i = 0, j = 0, mask = renames.mask; i <= mask; ++i) {
    uint64_t slot = renames.table[i];
    if (slot) {
      sorted_vers[j++] = (slot >> 32) | ((uint64_t)i << 32);
    }
  }
  call_qsort_r(sorted_vers, renames.count, sizeof(*sorted_vers), compare_ver_u, &renames);
  
  outbuf_t outbuf = {0};
  outbuf_emplace(&outbuf, (renames.mask + 1ull) * sizeof(uint64_t));
  for (uint32_t i = 0; i < renames.count; ++i) {
    uint64_t sv = sorted_vers[i];
    sht_t* contents = (sht_t*)sht_u_to_p(&renames, (uint32_t)sv);
    uint32_t ver_name_len = sht_p_key_length(contents);
    outbuf_emplace(&outbuf, ver_name_len + 1);
    if (outbuf.size & 7) {
      outbuf_emplace(&outbuf, 8 - (outbuf.size & 7));
    }
    uint32_t inner_offset = outbuf_emplace(&outbuf, sizeof(uint32_t) * 2 + (contents->mask + 1ull) * sizeof(uint64_t));
    ((uint64_t*)outbuf.bytes)[sv >> 32] = (uint32_t)renames.table[sv >> 32] + ((uint64_t)inner_offset << 32);
    ((uint32_t*)(outbuf.bytes + inner_offset))[0] = ver_name_len;
    ((uint32_t*)(outbuf.bytes + inner_offset))[1] = contents->mask;
    memcpy(outbuf.bytes + inner_offset - ver_name_len - 1, sht_p_key(contents), ver_name_len);
    inner_offset += sizeof(uint32_t) * 2;
    uint64_t* table = contents->table;
    for (uint32_t j = 0, mask = contents->mask; j <= mask; ++j) {
      uint64_t slot = table[j];
      if (slot) {
        parsed_name_t* slot_p = (parsed_name_t*)sht_u_to_p(contents, slot >> 32);
        outbuf_emplace(&outbuf, sht_p_key_length(slot_p));
        uint32_t value_offset = outbuf_emplace(&outbuf, 4);
        *(outbuf.bytes + value_offset + 2) = sht_p_key_length(slot_p);
        memcpy(outbuf.bytes + value_offset - sht_p_key_length(slot_p), sht_p_key(slot_p), sht_p_key_length(slot_p));
        ((uint64_t*)(outbuf.bytes + inner_offset))[j] = (uint32_t)slot + ((uint64_t)(value_offset - inner_offset) << 32);
        if (slot_p->lib.chars) {
          uint8_t new_lib_len = slot_p->lib.len;
          if (new_lib_len == 8 && memcmp(slot_p->lib.chars, "polyfill", 8) == 0) {
            *(outbuf.bytes + value_offset + 1) = '\xff';
          } else {
            *(outbuf.bytes + value_offset + 1) = new_lib_len + 1;
            memcpy(outbuf_emplace_p(&outbuf, new_lib_len + 1), slot_p->lib.chars, new_lib_len);
          }
        }
        if (slot_p->name.len != sht_p_key_length(slot_p) || memcmp(slot_p->name.chars, sht_p_key(slot_p), slot_p->name.len) != 0) {
          uint8_t new_name_len = slot_p->name.len;
          outbuf.bytes[value_offset] = new_name_len + 1;
          memcpy(outbuf_emplace_p(&outbuf, new_name_len + 1), slot_p->name.chars, new_name_len);
        }
        if (slot_p->version.chars) {
          uint8_t new_ver_len = slot_p->version.len;
          *(outbuf.bytes + value_offset + 3) = new_ver_len + 1;
          memcpy(outbuf_emplace_p(&outbuf, new_ver_len + 1), slot_p->version.chars, new_ver_len);
        }
      }
    }
    sht_free(contents);
  }

  free(sorted_vers);
  sht_free(&renames);
  tokeniser_free(&toks);

  FILE* f = fopen(argv[2], "w");
  if (!f) {
    FATAL("Could not open %s for writing", argv[2]);
  }
  char* var_name = strdup(argv[1]);
  for (char* itr = var_name; *itr; ++itr) {
    char c = *itr;
    if (!(('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9'))) {
      *itr = '_';
    }
  }
  fprintf(f, "#define %s_mask 0x%x\n", var_name, (unsigned)renames.mask);
  fprintf(f, "static const uint8_t %s_data[] __attribute__ ((aligned(8))) = {\n", var_name);
  free(var_name);
  for (uint32_t i = 0;;) {
    uint8_t c = (uint8_t)outbuf.bytes[i];
    if (0x20 <= c && c <= 0x7e && c != '\\' && c != '\'') {
      fprintf(f, "'%c', ", (char)c);
    } else {
      fprintf(f, "0x%02x, ", c);
    }
    if (++i >= outbuf.size) break;
    if (!(i & 127)) fprintf(f, "\n");
  }
  fprintf(f, "0};\n");
  fclose(f);
  free(outbuf.bytes);
  
  return 0;
}
