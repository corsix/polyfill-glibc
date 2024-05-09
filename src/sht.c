#include <string.h>
#include <stdlib.h>
#include "sht.h"

typedef struct sht_contents_meta_t {
  uint32_t size;
  uint32_t capacity;
  uint32_t value_length;
} sht_contents_meta_t;

#define getu32(p) (*(const uint32_t*)(p))
#define rol(x, n) (((x)<<(n)) | ((x)>>(-(int)(n)&(8*sizeof(x)-1))))

#if defined(__clang__)
__attribute__((no_sanitize("alignment")))
#endif
uint32_t sht_hash_mem(const char* str, uint32_t len) {
  uint32_t a, b, h = len;
  if (len > 12) {
    const char* pe = str + len - 12;
    const char* p = pe;
    const char* q = str;
    a = b = 0;
    do {
      b += getu32(p);
      h += getu32(p+4);
      a += getu32(p+8);
      p = q; q += 12;
      a ^= h; a -= rol(h, 14);
      b ^= a; b -= rol(a, 11);
      h ^= b; h -= rol(b, 25);
    } while (p < pe);
  } else if (len >= 4) {
    a = getu32(str);
    h ^= getu32(str+len-4);
    b = getu32(str+(len>>1)-2);
    h ^= b; h -= rol(b, 14);
    b += getu32(str+(len>>2)-1);
  } else if (len) {
    a = *(const uint8_t *)str;
    h ^= *(const uint8_t *)(str+len-1);
    b = *(const uint8_t *)(str+(len>>1));
    h ^= b; h -= rol(b, 14);
  } else {
    a = b = 0;
  }
  a ^= h; a -= rol(h, 11);
  b ^= a; b -= rol(a, 25);
  h ^= b; h -= rol(b, 16);
  return h;
}

void sht_init(sht_t* sht, uint32_t value_len) {
  sht_contents_meta_t* meta = (sht_contents_meta_t*)(sht->contents = malloc(64));
  meta->size = sizeof(*meta) / sizeof(uint32_t);
  meta->capacity = 64 / sizeof(uint32_t);
  meta->value_length = (value_len + 3) / (uint32_t)sizeof(uint32_t);
  sht->count = 0;
  sht->table = calloc((sht->mask = 3) + 1, sizeof(uint64_t));
}

void sht_clear(sht_t* sht) {
  if (sht->count) {
    sht->count = 0;
    ((sht_contents_meta_t*)sht->contents)->size = sizeof(sht_contents_meta_t) / sizeof(uint32_t);
    memset(sht->table, 0, (sht->mask + 1ull) * sizeof(uint64_t));
  }
}

void sht_free(sht_t* sht) {
  free(sht->contents);
  free(sht->table);
}

static uint32_t add_to_contents(sht_t* sht, const char* key, uint32_t key_len) {
  uint32_t* contents = sht->contents;
  sht_contents_meta_t* meta = (sht_contents_meta_t*)contents;
  uint32_t result_u = meta->size + (key_len + (uint32_t)(sizeof(uint32_t) * 2)) / (uint32_t)sizeof(uint32_t);
  uint32_t new_size = result_u + meta->value_length;
  if (new_size > meta->capacity) {
    uint32_t new_capacity = meta->capacity * 2;
    if (new_capacity < new_size) new_capacity = new_size;
    sht->contents = contents = (uint32_t*)realloc(contents, new_capacity * sizeof(uint32_t));
    meta = (sht_contents_meta_t*)contents;
    meta->capacity = new_capacity;
  }
  meta->size = new_size;
  contents += result_u;
  memset(contents, 0, meta->value_length * sizeof(uint32_t));
  contents[-1] = key_len;
  contents[-2] = 0;
  memcpy((char*)(contents - 1) - 1 - key_len, key, key_len);
  return result_u;
}

static void reinsert(uint64_t* table, uint32_t mask, uint64_t hv, uint32_t d) {
  for (;; ++d) {
    uint32_t idx = ((uint32_t)hv + d) & mask;
    uint64_t slot = table[idx];
    if (!slot) {
      table[idx] = hv;
      break;
    }
    uint32_t d2 = (idx - (uint32_t)slot) & mask;
    if (d2 < d) {
      table[idx] = hv;
      hv = slot;
      d = d2;
    }
  }
}

static void rehash(sht_t* sht) {
  uint64_t* table = sht->table;
  uint32_t mask = sht->mask;
  uint32_t new_mask = mask * 2 + 1;
  uint64_t* new_table = calloc(new_mask + 1ull, sizeof(uint64_t));
  uint32_t idx = 0;
  do {
    uint64_t slot = table[idx];
    if (slot) {
      reinsert(new_table, new_mask, slot, 0);
    }
  } while (idx++ != mask);
  sht->table = new_table;
  sht->mask = new_mask;
  free(table);
}

uint32_t sht_intern_u(sht_t* sht, const char* key, uint32_t key_len) {
  uint32_t hash = sht_hash_mem(key, key_len);
  uint64_t* table = sht->table;
  uint32_t mask = sht->mask;
  uint32_t* contents = sht->contents;
  uint32_t d = 0;
  for (;; ++d) {
    uint32_t idx = ((uint32_t)hash + d) & mask;
    uint64_t slot = table[idx];
    if (!slot) {
      uint32_t result_u = add_to_contents(sht, key, key_len);
      table[idx] = hash | ((uint64_t)result_u << 32);
      if (++sht->count * 4ull >= sht->mask * 3ull) {
        rehash(sht);
      }
      return result_u;
    } else if ((uint32_t)slot == hash) {
      uint32_t result = slot >> 32;
      uint32_t* ptr = contents + result;
      if (ptr[-1] == key_len && !memcmp((char*)(ptr - 1) - 1 - key_len, key, key_len)) {
        return result;
      }
    } else {
      uint32_t d2 = (idx - (uint32_t)slot) & mask;
      if (d2 < d) {
        uint32_t result_u = add_to_contents(sht, key, key_len);
        table[idx] = hash | ((uint64_t)result_u << 32);
        reinsert(table, mask, slot, d2);
        if (++sht->count * 4ull >= sht->mask * 3ull) {
          rehash(sht);
        }
        return result_u;
      }
    }
  }
}

void* sht_intern_p(sht_t* sht, const char* key, uint32_t key_len) {
  uint32_t u = sht_intern_u(sht, key, key_len);
  return sht_u_to_p(sht, u);
}

void* sht_lookup_p(const sht_t* sht, const char* key, uint32_t key_len) {
  uint32_t hash = sht_hash_mem(key, key_len);
  uint64_t* table = sht->table;
  uint32_t mask = sht->mask;
  uint32_t* contents = sht->contents;
  uint32_t d = 0;
  for (;; ++d) {
    uint32_t idx = ((uint32_t)hash + d) & mask;
    uint64_t slot = table[idx];
    if (!slot) {
      return NULL;
    } else if ((uint32_t)slot == hash) {
      uint32_t* result = contents + (slot >> 32);
      if (result[-1] == key_len && !memcmp((char*)(result - 1) - 1 - key_len, key, key_len)) {
        return result;
      }
    } else if (((idx - (uint32_t)slot) & mask) < d) {
      return NULL;
    }
  }
}

uint32_t sht_lookup_u(const sht_t* sht, const char* key, uint32_t key_len) {
  uint32_t hash = sht_hash_mem(key, key_len);
  uint64_t* table = sht->table;
  uint32_t mask = sht->mask;
  uint32_t* contents = sht->contents;
  uint32_t d = 0;
  for (;; ++d) {
    uint32_t idx = ((uint32_t)hash + d) & mask;
    uint64_t slot = table[idx];
    if (!slot) {
      return 0;
    } else if ((uint32_t)slot == hash) {
      uint32_t result = slot >> 32;
      uint32_t* ptr = contents + result;
      if (ptr[-1] == key_len && !memcmp((char*)(ptr - 1) - 1 - key_len, key, key_len)) {
        return result;
      }
    } else if (((idx - (uint32_t)slot) & mask) < d) {
      return 0;
    }
  }
}

void* sht_iter_start_p(const sht_t* sht) {
  uint32_t* contents = sht->contents;
  sht_contents_meta_t* meta = (sht_contents_meta_t*)contents;
  uint32_t top = meta->size;
  if (top <= sizeof(*meta) / sizeof(uint32_t)) return NULL;
  return contents + (top - meta->value_length);
}

void* sht_iter_next_p(const sht_t* sht, void* p) {
  uint32_t* pu = (uint32_t*)p;
  uint32_t* contents = sht->contents;
  sht_contents_meta_t* meta = (sht_contents_meta_t*)contents;
  pu -= (pu[-1] + (uint32_t)(sizeof(uint32_t) * 2)) / sizeof(uint32_t);
  if (pu <= (uint32_t*)(meta + 1)) return NULL;
  return pu - meta->value_length;
}

const char* sht_p_key(void* p) {
  uint32_t* pu = (uint32_t*)p;
  return (const char*)(pu - 1) - 1 - pu[-1];
}
