// uuht.h: u32/u32 hash table.
// Can have key being zero, or value being zero, but not both at the same time.
#pragma once
#include <stdint.h>

typedef struct uuht_t {
  uint64_t* table; // Hash in low u32, value in high u32. Empty slots contain zero. Use uuht_unhash to turn hash back into key.
  uint32_t mask;   // Power of two minus one.
  uint32_t count;  // Number of non-zero entries in table.
} uuht_t;

void uuht_init(uuht_t* uuht);
void uuht_clear(uuht_t* uuht);
void uuht_free(uuht_t* uuht);

uint32_t uuht_set(uuht_t* uuht, uint32_t k, uint32_t v); // Sets k to v, returns old v (or zero if no prior v for k).
uint32_t uuht_set_if_absent(uuht_t* uuht, uint32_t k, uint32_t v); // If k not present, sets it to v and returns 0. Otherwise returns existing value of k.

uint32_t uuht_lookup_or(const uuht_t* uuht, uint32_t k, uint32_t default_v); // If k not present, returns default_v. Otherwise returns v.
uint32_t uuht_lookup_or_set(uuht_t* uuht, uint32_t k, uint32_t v); // If k not present, sets k to v. Returns the value associated with k.
uint64_t uuht_contains(const uuht_t* uuht, uint32_t k); // If k not present, returns 0. Otherwise returns `(v << 32) | hash(k)`, which is non-zero.

uint32_t uuht_unhash(uint32_t h);
