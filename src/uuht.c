#include "uuht.h"
#include <stdlib.h>
#include <string.h>

void uuht_init(uuht_t* uuht) {
  uuht->count = 0;
  uuht->table = calloc((uuht->mask = 7) + 1, sizeof(uint64_t));
}

void uuht_free(uuht_t* uuht) {
  free(uuht->table);
}

static uint32_t uuht_hash(uint32_t h) {
  // Must map 0 to 0, and must be invertible.
  // From https://github.com/skeeto/hash-prospector/issues/19
  h ^= h >> 16;
  h *= 0x21f0aaad;
  h ^= h >> 15;
  h *= 0xd35a2d97;
  h ^= h >> 15;
  return h;
}

uint32_t uuht_unhash(uint32_t h) {
  h ^= h >> 15;
  h ^= h >> 30;
  h *= 0x37132227;
  h ^= h >> 15;
  h ^= h >> 30;
  h *= 0x333c4925;
  h ^= h >> 16;
  return h;
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

static void uuht_rehash(uuht_t* uuht) {
  uint32_t old_mask = uuht->mask;
  uint32_t new_mask = old_mask * 2 + 1;
  uint64_t* new_table = calloc(new_mask + 1ull, sizeof(uint64_t));
  uint64_t* old_table = uuht->table;
  uint32_t idx = 0;
  do {
    uint64_t slot = old_table[idx];
    if (slot) {
      reinsert(new_table, new_mask, slot, 0);
    }
  } while (idx++ != old_mask);
  uuht->table = new_table;
  uuht->mask = new_mask;
  free(old_table);
}

uint64_t uuht_contains(const uuht_t* uuht, uint32_t k) {
  k = uuht_hash(k);
  uint64_t* table = uuht->table;
  uint32_t mask = uuht->mask;
  uint32_t d = 0;
  for (;; ++d) {
    uint32_t idx = ((uint32_t)k + d) & mask;
    uint64_t slot = table[idx];
    if (!slot || (uint32_t)slot == k) {
      return slot;
    } else if (((idx - (uint32_t)slot) & mask) < d) {
      return 0;
    }
  }
}

uint32_t uuht_lookup_or(const uuht_t* uuht, uint32_t k, uint32_t default_v) {
  k = uuht_hash(k);
  uint64_t* table = uuht->table;
  uint32_t mask = uuht->mask;
  uint32_t d = 0;
  for (;; ++d) {
    uint32_t idx = ((uint32_t)k + d) & mask;
    uint64_t slot = table[idx];
    if (!slot) {
      return default_v;
    } else if ((uint32_t)slot == k) {
      return slot >> 32;
    } else if (((idx - (uint32_t)slot) & mask) < d) {
      return default_v;
    }
  }
}

uint32_t uuht_lookup_or_set(uuht_t* uuht, uint32_t k, uint32_t v) {
  k = uuht_hash(k);
  uint64_t* table = uuht->table;
  uint32_t mask = uuht->mask;
  uint32_t d = 0;
  for (;; ++d) {
    uint32_t idx = ((uint32_t)k + d) & mask;
    uint64_t slot = table[idx];
    if (!slot) {
      slot = k | (((uint64_t)v) << 32);
      table[idx] = slot;
      if (slot && ++uuht->count * 4ull >= mask * 3ull) {
        uuht_rehash(uuht);
      }
      return v;
    } else if ((uint32_t)slot == k) {
      return slot >> 32;
    } else {
      uint32_t d2 = (idx - (uint32_t)slot) & mask;
      if (d2 < d) {
        if (k | v) {
          table[idx] = k | (((uint64_t)v) << 32);
          reinsert(table, mask, slot, d2);
          if (++uuht->count * 4ull >= mask * 3ull) {
            uuht_rehash(uuht);
          }
        }
        return v;
      }
    }
  }
}

void uuht_clear(uuht_t* uuht) {
  uuht->count = 0;
  memset(uuht->table, 0, (uuht->mask + 1ull) * sizeof(uint64_t));
}

uint32_t uuht_set(uuht_t* uuht, uint32_t k, uint32_t v) {
  k = uuht_hash(k);
  uint64_t* table = uuht->table;
  uint32_t mask = uuht->mask;
  uint32_t d = 0;
  for (;; ++d) {
    uint32_t idx = ((uint32_t)k + d) & mask;
    uint64_t slot = table[idx];
    if (!slot) {
      slot = k | (((uint64_t)v) << 32);
      table[idx] = slot;
      if (slot && ++uuht->count * 4ull >= mask * 3ull) {
        uuht_rehash(uuht);
      }
      return 0;
    } else if ((uint32_t)slot == k) {
      uint32_t old_v = (uint32_t)(slot >> 32);
      slot = k | (((uint64_t)v) << 32);
      if (slot) {
        table[idx] = slot;
      }
      return old_v;
    } else {
      uint32_t d2 = (idx - (uint32_t)slot) & mask;
      if (d2 < d) {
        if (k | v) {
          table[idx] = k | (((uint64_t)v) << 32);
          reinsert(table, mask, slot, d2);
          if (++uuht->count * 4ull >= mask * 3ull) {
            uuht_rehash(uuht);
          }
        }
        return 0;
      }
    }
  }
}

uint32_t uuht_set_if_absent(uuht_t* uuht, uint32_t k, uint32_t v) {
  k = uuht_hash(k);
  uint64_t* table = uuht->table;
  uint32_t mask = uuht->mask;
  uint32_t d = 0;
  for (;; ++d) {
    uint32_t idx = ((uint32_t)k + d) & mask;
    uint64_t slot = table[idx];
    if (!slot) {
      slot = k | (((uint64_t)v) << 32);
      table[idx] = slot;
      if (slot && ++uuht->count * 4ull >= mask * 3ull) {
        uuht_rehash(uuht);
      }
      return 0;
    } else if ((uint32_t)slot == k) {
      return (uint32_t)(slot >> 32);
    } else {
      uint32_t d2 = (idx - (uint32_t)slot) & mask;
      if (d2 < d) {
        if (k | v) {
          table[idx] = k | (((uint64_t)v) << 32);
          reinsert(table, mask, slot, d2);
          if (++uuht->count * 4ull >= mask * 3ull) {
            uuht_rehash(uuht);
          }
        }
        return 0;
      }
    }
  }
}
