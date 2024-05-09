// v2f.h: Sorted map from uint64_t to uint64_t, typically used for storing
// the map from virtual address to file offset.

#pragma once
#include <stdint.h>

typedef struct v2f_entry_t {
  uint64_t vbase;
  uint64_t v2f; // Note special values V2F_ZEROFILL, V2F_UNMAPPED
} v2f_entry_t;

#define V2F_ZEROFILL 1
#define V2F_UNMAPPED 2
#define V2F_SPECIAL (V2F_ZEROFILL | V2F_UNMAPPED)

typedef struct v2f_map_t {
  v2f_entry_t* cached;
  v2f_entry_t* entries;
  uint32_t count;
  uint32_t capacity;
} v2f_map_t;

void v2f_map_init(v2f_map_t* m, uint64_t initial_v2f);
void v2f_map_set_range(v2f_map_t* m, uint64_t vbase, uint64_t size, uint64_t v2f);
v2f_entry_t* v2f_map_lookup(v2f_map_t* m, uint64_t v);
v2f_entry_t* v2f_map_lookup_f_range(v2f_map_t* m, uint64_t f, uint64_t size);
