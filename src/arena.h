#pragma once
#include <stddef.h>

typedef struct arena_t {
  struct arena_region_t* used;
  struct arena_region_t* avail;
  size_t bump;
} arena_t;

void* arena_alloc(arena_t* arena, size_t size);
void arena_reset(arena_t* arena);
void arena_free(arena_t* arena);
