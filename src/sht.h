// sht.h: String hash table.
// Keys are arbitrary-length byte arrays.
// Values are fixed-length byte arrays (length specified at sht
// creation time), default-initialised to all zeroes.

#pragma once
#include <stdint.h>

// Hash function used by sht; made available for anyone else that wants it.
uint32_t sht_hash_mem(const char* str, uint32_t len);

typedef struct sht_t {
  uint32_t* contents; // Metadata, then repeats of: padding, key, nul, u32 key_length, value.
  uint64_t* table;    // Hash in low u32, (value offset / 4) in high u32. Empty slots contain zero.
  uint32_t mask;      // Power of two minus one.
  uint32_t count;     // Number of non-zero entries in table.
} sht_t;

const char* sht_p_key(void* p);
#define sht_u_to_p(sht, u) ((void*)&((sht)->contents[(u)]))
#define sht_p_key_length(p) (((uint32_t*)(p))[-1])
#define sht_u_key(sht, u) (sht_p_key(sht_u_to_p((sht), (u))))
#define sht_u_key_length(sht, u) (sht_p_key_length(sht_u_to_p((sht), (u))))

void sht_init(sht_t* sht, uint32_t value_len);
void sht_clear(sht_t* sht);
void sht_free(sht_t* sht);

void* sht_intern_p(sht_t* sht, const char* key, uint32_t key_len); /* If not present, adds new entry with all-zero value. In all cases returns pointer to value (which will be non-NULL). */
uint32_t sht_intern_u(sht_t* sht, const char* key, uint32_t key_len); /* If not present, adds new entry with all-zero value. In all cases returns u32 index of value (which will be non-zero). */

void* sht_lookup_p(const sht_t* sht, const char* key, uint32_t key_len); /* If not present, returns NULL. */
uint32_t sht_lookup_u(const sht_t* sht, const char* key, uint32_t key_len); /* If not present, returns 0. */

void* sht_iter_start_p(const sht_t* sht);
void* sht_iter_next_p(const sht_t* sht, void* p);
