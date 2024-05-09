#include "arena.h"
#include <stdlib.h>

typedef struct arena_region_t {
  struct arena_region_t* next;
  size_t size;
} arena_region_t;

void* arena_alloc(arena_t* arena, size_t size) {
  size_t bump = arena->bump;
  for (;;) {
    if (bump > size) {
      bump -= size;
      bump &= ~(size_t)(sizeof(void*) - 1);
      if (bump >= sizeof(arena_region_t)) {
        arena->bump = bump;
        return (char*)arena->used + bump;
      }
    }
    arena_region_t* a = arena->avail;
    if (a) {
      arena->avail = a->next;
      bump = a->size;
    } else {
      bump = sizeof(arena_region_t) + (size < 512 ? 512 : size);
      a = (arena_region_t*)malloc(bump);
      a->size = bump;
    }
    a->next = arena->used;
    arena->used = a;
  }
}

void arena_reset(arena_t* arena) {
  arena_region_t* u = arena->used;
  arena_region_t* a = arena->avail;
  while (u) {
    arena_region_t* next = u->next;
    u->next = a;
    a = u;
    u = next;
  }
  arena->used = u;
  arena->avail = a;
  arena->bump = 0;
}

void arena_free(arena_t* arena) {
  arena_region_t* r = arena->used;
  while (r) {
    arena_region_t* next = r->next;
    free(r);
    r = next;
  }
  r = arena->avail;
  while (r) {
    arena_region_t* next = r->next;
    free(r);
    r = next;
  }
  arena->used = NULL;
  arena->avail = NULL;
  arena->bump = 0;
}
