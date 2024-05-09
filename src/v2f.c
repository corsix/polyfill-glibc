#include <stdlib.h>
#include <string.h>
#include "v2f.h"

void v2f_map_init(v2f_map_t* m, uint64_t initial_v2f) {
  if (!m->capacity) {
    m->capacity = 4;
    m->entries = m->cached = malloc(sizeof(v2f_entry_t) * m->capacity);
  }
  m->count = 1;
  m->entries[0].vbase = 0;
  m->entries[0].v2f = initial_v2f;
  m->entries[1].vbase = 0;
  m->entries[1].v2f = initial_v2f;
}

void v2f_map_set_range(v2f_map_t* m, uint64_t vbase, uint64_t size, uint64_t v2f) {
  if (!size) return;
  if (m->count + 2 >= m->capacity) {
    uint32_t new_capacity = m->capacity * 2;
    v2f_entry_t* new_entries = realloc(m->entries, sizeof(v2f_entry_t) * new_capacity);
    m->entries = m->cached = new_entries;
    m->capacity = new_capacity;
  }
  v2f_entry_t* last = m->entries + m->count;
  v2f_entry_t* tail = last - 1;
  uint64_t vend = vbase + size - 1;
  while (vend < tail->vbase) --tail;
  /* The range ends in tail, i.e. tail->vbase <= vend, vend <= tail[1].vbase - 1. */
  v2f_entry_t* itr = tail;
  while (vbase < itr->vbase) --itr;
  /* The range starts in itr, i.e. itr->vbase <= vbase, vbase <= itr[1].vbase - 1. */
  /* Remove redundant entries at itr. */
  if (itr->vbase != vbase) ++itr;
  if (vbase != 0 && itr[-1].v2f == v2f) {
    vbase = itr[-1].vbase;
    --itr;
  }
  /* Remove redundant entries at tail. */
  if (tail < last && tail[1].vbase - 1 == vend) ++tail;
  if (tail < last && tail->v2f == v2f) {
    vend = tail[1].vbase - 1;
    ++tail;
  }
  /* Make space for new record. */
  if (itr + 1 != tail) {
    size_t nb = (char*)(last + 1) - (char*)tail;
    memmove(itr + 1, tail, nb);
    m->count = (v2f_entry_t*)((char*)itr + nb) - m->entries;
    m->cached = itr;
  }
  /* Insert new record. */
  itr->vbase = vbase;
  itr->v2f = v2f;
  itr[1].vbase = vend + 1;
}

v2f_entry_t* v2f_map_lookup(v2f_map_t* m, uint64_t v) {
  v2f_entry_t* cached = m->cached;
  if (v >= cached->vbase) {
    if (v <= (cached[1].vbase - 1)) {
      return cached;
    }
    do {
      ++cached;
    } while (v > (cached[1].vbase - 1));
  } else {
    do {
      --cached;
    } while (v < cached->vbase);
  }
  m->cached = cached;
  return cached;
}

v2f_entry_t* v2f_map_lookup_f_range(v2f_map_t* m, uint64_t f, uint64_t size) {
  v2f_entry_t* itr = m->entries;
  v2f_entry_t* end = itr + m->count;
  for (; itr < end; ++itr) {
    if (itr->v2f & V2F_SPECIAL) continue;
    uint64_t itrf = itr->vbase + itr->v2f;
    if (f < itrf) continue;
    uint64_t itr1f = itr[1].vbase + itr->v2f;
    if (itr1f <= f) continue;
    if ((itr1f - f) < size) continue;
    return itr;
  }
  return NULL;
}
