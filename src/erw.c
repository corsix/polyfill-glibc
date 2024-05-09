#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>
#include "erw.h"
#include "reloc_kind.h"
#include "common.h"
#include "sht.h"
#include "polyfiller.h"

static void erw_phdrs_increase_capacity(erw_state_t* erw) {
  void* base = erw->phdrs.base;
  size_t esz = (erw->file_kind & ERW_FILE_KIND_64) ? sizeof(struct Elf64_Phdr) : sizeof(struct Elf32_Phdr);
  if (erw->phdrs.capacity) {
    uint32_t new_capacity = erw->phdrs.capacity * 2;
    erw->phdrs.base = realloc(base, new_capacity * esz);
    erw->phdrs.capacity = new_capacity;
  } else if (erw->phdrs.count) {
    uint32_t new_capacity = erw->phdrs.count * 2;
    erw->phdrs.base = malloc(new_capacity * esz);
    memcpy(erw->phdrs.base, base, erw->phdrs.count * esz);
    erw->phdrs.capacity = new_capacity;
  } else {
    uint32_t new_capacity = 4;
    erw->phdrs.base = malloc(new_capacity * esz);
    erw->phdrs.capacity = new_capacity;
  }
}

static uint32_t extra_phdrs_required_for_category_mask(uint32_t category_mask) {
  uint32_t result = 0;
  if (category_mask & ~(1u << erw_alloc_category_f)) {
    result += 2;
  }
  if (category_mask & (1u << erw_alloc_category_v_rx)) {
    result += 1;
  }
  if (category_mask & ((1u << erw_alloc_category_v_rw) | (1u << erw_alloc_category_v_rw_zero))) {
    result += 1;
  }
  return result;
}

static void erw_dhdrs_increase_capacity(erw_state_t* erw) {
  void* base = erw->dhdrs.base;
  size_t esz = (erw->file_kind & ERW_FILE_KIND_64) ? sizeof(struct Elf64_Dyn) : sizeof(struct Elf32_Dyn);
  if (erw->dhdrs.capacity) {
    uint32_t new_capacity = erw->dhdrs.capacity * 2;
    erw->dhdrs.base = realloc(base, new_capacity * esz);
    erw->dhdrs.capacity = new_capacity;
  } else if (erw->dhdrs.count) {
    uint32_t new_capacity = erw->dhdrs.count * 2;
    erw->dhdrs.base = malloc(new_capacity * esz);
    memcpy(erw->dhdrs.base, base, erw->dhdrs.count * esz);
    erw->dhdrs.capacity = new_capacity;
  } else {
    uint32_t new_capacity = 4;
    erw->dhdrs.base = malloc(new_capacity * esz);
    erw->dhdrs.capacity = new_capacity;
  }
}

static int cmp_alloc_bucket(const void* lhs, const void* rhs) {
  const erw_alloc_bucket_t* bl = (erw_alloc_bucket_t*)lhs;
  const erw_alloc_bucket_t* br = (erw_alloc_bucket_t*)rhs;
  if (bl->category != br->category) return bl->category - br->category;
  return (int)bl->alignment - (int)br->alignment;
}

static bool materialise_v_range_to(erw_state_t* erw, uint64_t vbase, uint64_t len, char* dst) {
  uint32_t n_memset = 0;
  uint32_t n_memcpy = 0;
  if (len) {
    v2f_entry_t* e = v2f_map_lookup(&erw->v2f, vbase);
    for (;;) {
      uint64_t len_here = e[1].vbase - vbase;
      if (len_here > len) {
        len_here = len;
      }
      if (e->v2f & V2F_SPECIAL) {
        ++n_memset;
        memset(dst, 0, len_here);
      } else {
        ++n_memcpy;
        memcpy(dst, erw->f + (e->v2f + vbase), len_here);
      }
      len -= len_here;
      if (!len) break;
      dst += len_here;
      vbase += len_here;
      ++e;
    }
  }
  return n_memcpy == 1 && n_memset == 0; // Returns true if a single contiguous segment.
}

static uint32_t materialise_v_u32(erw_state_t* erw, uint64_t vbase) {
  uint32_t result;
  materialise_v_range_to(erw, vbase, sizeof(result), (char*)&result);
  if (erw->file_kind & ERW_FILE_KIND_BSWAP) result = __builtin_bswap32(result);
  return result;
}

static void erw_relocs_increase_capacity(reloc_editor_t* editor) {
  void* base = editor->base;
  if (editor->capacity) {
    uint32_t new_capacity = editor->capacity * 2;
    editor->base = realloc(base, new_capacity * editor->entry_size);
    editor->capacity = new_capacity;
  } else if (editor->count) {
    uint32_t new_capacity = editor->count * 2;
    editor->base = malloc(new_capacity * editor->entry_size);
    memcpy(editor->base, base, editor->count * editor->entry_size);
    editor->capacity = new_capacity;
  } else {
    uint32_t new_capacity = 4;
    editor->base = malloc(new_capacity * editor->entry_size);
    editor->capacity = new_capacity;
  }
}

static void erw_dsyms_increase_capacity(erw_state_t* erw) {
  size_t esz = erw->file_kind & ERW_FILE_KIND_64 ? sizeof(struct Elf64_Sym) : sizeof(struct Elf32_Sym);
  void* base = erw->dsyms.base;
  uint16_t* vers = erw->dsyms.versions;
  sym_info_t* info = erw->dsyms.info;
  erw->modified |= ERW_MODIFIED_DSYMS | ERW_MODIFIED_HASH_TABLES;
  if (erw->dsyms.capacity) {
    uint32_t new_capacity = erw->dsyms.capacity * 2;
    erw->dsyms.base = realloc(base, new_capacity * esz);
    erw->dsyms.info = realloc(info, new_capacity * sizeof(*info));
    if (vers) erw->dsyms.versions = realloc(vers, new_capacity * sizeof(*vers));
    erw->dsyms.capacity = new_capacity;
  } else if (erw->dsyms.count) {
    uint32_t new_capacity = erw->dsyms.count * 2;
    erw->dsyms.base = malloc(new_capacity * esz);
    memcpy(erw->dsyms.base, base, erw->dsyms.count * esz);
    erw->dsyms.info = realloc(info, new_capacity * sizeof(*info));
    if (vers) {
      erw->dsyms.versions = malloc(new_capacity * sizeof(*vers));
      memcpy(erw->dsyms.versions, vers, erw->dsyms.count * sizeof(*vers));
    }
    erw->dsyms.capacity = new_capacity;
  } else {
    uint32_t new_capacity = 4;
    erw->dsyms.base = malloc(new_capacity * esz);
    erw->dsyms.info = malloc(new_capacity * sizeof(*info));
    erw->dsyms.capacity = new_capacity;
  }
}

static uint32_t sym_info_hash_count(erw_state_t* erw, uint32_t bit) {
  sym_info_t* itr = erw->dsyms.info;
  sym_info_t* end = itr + erw->dsyms.count;
  uint32_t result = 0;
  for (; itr < end; ++itr) {
    uint32_t flags = itr->flags;
    if (!(flags & SYM_INFO_LOCAL) && (flags & SYM_INFO_EXPORTABLE) && (flags & bit)) {
      result += 1;
    }
  }
  return result;
}

static uint32_t bit_ceil_u32_log2(uint32_t x) {
  return 32 - __builtin_clz(x | 1) - !(x & (x - 1));
}

static void vers_ensure_vaddrs(erw_state_t* erw, ver_group_t* groups, size_t nb_group, size_t nb_item, const char* section_name);

static ver_group_t* vers_erase_pending_deletes(ver_group_t* g) {
  while (g && g->state & VER_EDIT_STATE_PENDING_DELETE) g = g->next;
  ver_group_t* result = g;
  while (g) {
    ver_group_t* gn = g->next;
    if (gn && (gn->state & (VER_EDIT_STATE_DIRTY | VER_EDIT_STATE_PENDING_DELETE))) {
      g->state |= VER_EDIT_STATE_DIRTY;
      while (gn && (gn->state & VER_EDIT_STATE_PENDING_DELETE)) gn = gn->next;
      g->next = gn;
    }
    ver_item_t* i = g->items;
    if (i && (i->state & (VER_EDIT_STATE_DIRTY | VER_EDIT_STATE_PENDING_DELETE))) {
      g->state |= VER_EDIT_STATE_DIRTY;
      while (i && (i->state & VER_EDIT_STATE_PENDING_DELETE)) i = i->next;
      g->items = i;
    }
    while (i) {
      ver_item_t* in = i->next;
      if (in && (in->state & (VER_EDIT_STATE_DIRTY | VER_EDIT_STATE_PENDING_DELETE))) {
        i->state |= VER_EDIT_STATE_DIRTY;
        while (in && (in->state & VER_EDIT_STATE_PENDING_DELETE)) in = in->next;
        i->next = in;
      }
      i = in;
    }
    g = gn;
  }
  return result;
}

static uint32_t vers_g_count(ver_group_t* g) {
  uint32_t result = 0;
  for (; g; g = g->next) {
    ++result;
  }
  return result;
}

static uint32_t vers_i_count(ver_item_t* i) {
  uint32_t result = 0;
  for (; i; i = i->next) {
    ++result;
  }
  return result;
}

static void free_editors_common(erw_state_t* erw) {
  if (erw->dhdrs.base) {
    if (erw->dhdrs.capacity) free(erw->dhdrs.base);
    erw->dhdrs.base = NULL;
  }
  if (erw->dynstr.state == dynstr_edit_state_encode_building_new) {
    erw->dynstr.state = dynstr_edit_state_read_only;
  }
  if (erw->dsyms.base) {
    if (erw->dsyms.capacity) {
      free(erw->dsyms.base);
      free(erw->dsyms.versions);
    }
    free(erw->dsyms.info);
    erw->dsyms.base = NULL;
  }
  if (erw->vers.count) {
    erw->vers.count = 0;
    erw->vers.def = NULL;
    erw->vers.need = NULL;
  }
  {
    reloc_editor_t* editor = erw->relocs;
    for (; editor < erw->relocs + MAX_RELOC_EDITORS; ++editor) {
      if (!editor->base) break;
      if (editor->capacity) free(editor->base);
      editor->base = NULL;
    }
  }
}

static bool erw_do_mmap(erw_state_t* erw, int fd) {
  void* f = mmap(NULL, erw->f_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (f == MAP_FAILED) {
    return false;
  }
  if (erw->original) {
    // If requesting a view larger than the original file size, then we'll
    // get virtual memory up to the next page boundary, but will hit SIGBUS
    // if trying to access anything beyond that. Hence we re-map anything
    // beyond this point with anonymous zero-pages.
    static size_t host_page_size = 0;
    if (host_page_size == 0) host_page_size = sysconf(_SC_PAGESIZE);
    uint64_t page_end = (erw->original->f_size + host_page_size - 1) &- host_page_size;
    if (page_end < erw->f_size) {
      if (mmap(f + page_end, erw->f_size - page_end, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
        // 1st mmap succeeded, but 2nd one failed. Undo the 1st and pretend it failed too.
        munmap(f, erw->f_size);
        return false;
      }
    }
  }
  erw->f = f;
  erw->mmap_size = erw->f_size;
  erw->file_kind |= ERW_FILE_KIND_MMAPED;
  return true;
}

static void erw_reinit_f(erw_state_t* erw) {
  if (erw->file_kind & ERW_FILE_KIND_MMAPED) {
    erw->file_kind -= ERW_FILE_KIND_MMAPED;
    munmap(erw->f, erw->mmap_size);
    erw->mmap_size = 0;
    if (erw_do_mmap(erw, erw->original->fd)) {
      return;
    }
  } else {
    free(erw->f);
    erw->f = NULL;
  }
  erw->f = calloc(1, erw->f_size);
  if (erw->orig_mem) {
    memcpy(erw->f, erw->orig_mem, erw->original->f_size);
    return;
  }
  size_t goal = erw->original->f_size;
  size_t consumed = 0;
  int fd = erw->original->fd;
  for (;;) {
    ssize_t n = read(fd, erw->f + consumed, goal - consumed);
    if (n >= 0) {
      consumed += n;
      if (consumed == goal) break;
      if (n == 0) {
        FATAL("Encountered unexpected EOF while re-reading file");
      }
    } else if (errno != EINTR) {
      FATAL("Encountered unexpected error while re-reading file");
    }
  }
  if (lseek(fd, 0, SEEK_SET) != 0) {
    // Couldn't rewind? Take a copy for next time.
    void* orig_copy = malloc(goal);
    memcpy(orig_copy, erw->f, goal);
    erw->orig_mem = orig_copy;
  }
}

static void verdef_sweep_removed_refs(ver_group_t* g) {
  for (; g; g = g->next) {
    ver_item_t* i = g->items;
    if (i && (i->state & VER_EDIT_STATE_REMOVED_REFS)) {
      g->state |= VER_EDIT_STATE_PENDING_DELETE;
    }
  }
}

static void verneed_removing(erw_state_t* erw, ver_group_t* g, ver_item_t* i);

static void verneed_sweep_removed_refs(erw_state_t* erw, ver_group_t* g) {
  for (; g; g = g->next) {
    ver_item_t* i = g->items;
    if (i) {
      if (g->state & VER_EDIT_STATE_PENDING_DELETE) {
        if (strstr(erw_dynstr_decode(erw, g->name), "libc")) {
          do {
            verneed_removing(erw, g, i);
          } while ((i = i->next));
        }
      } else {      
        bool keep_g = false;
        do {
          if (i->state & (VER_EDIT_STATE_REMOVED_REFS | VER_EDIT_STATE_PENDING_DELETE)) {
            i->state = (i->state & ~VER_EDIT_STATE_REMOVED_REFS) | VER_EDIT_STATE_PENDING_DELETE;
            verneed_removing(erw, g, i);
          } else {
            keep_g = true;
          }
        } while ((i = i->next));
        if (!keep_g) {
          g->state |= VER_EDIT_STATE_PENDING_DELETE;
        }
      }
    }
  }
}

static const char* trim_leading_underscore(const char* str) {
  while (*str == '_') ++str;
  return str;
}

static void add_verdef(erw_state_t* erw, const char* ver_name, uint32_t index);
static void erw_dynstr_init(erw_state_t* erw);
static void erw_dynstr_build_table(erw_state_t* erw);
static void erw_dynstr_start_building_new(erw_state_t* erw);

#define NNE_INSTANTIATE "erw_nne.h"
#include "nne_instantiator.h"

// alloc

static erw_alloc_bucket_t* find_alloc_bucket(erw_alloc_buckets_t* buckets, enum erw_alloc_category category, uint32_t alignment) {
  erw_alloc_bucket_t* itr = buckets->base;
  erw_alloc_bucket_t* end = itr + buckets->count;
  for (; itr != end; ++itr) {
    if (itr->category == category && itr->alignment == alignment) {
      return itr;
    }
  }
  buckets->category_mask |= (1u << category);
  uint32_t capacity = buckets->capacity;
  if (buckets->count == capacity) {
    capacity = capacity ? (capacity * 2) : 4;
    itr = realloc(buckets->base, capacity * sizeof(*itr));
    buckets->base = itr;
    buckets->capacity = capacity;
  }
  itr = buckets->base + buckets->count++;
  itr->category = category;
  itr->alignment = alignment;
  itr->start = itr->current = itr->end = 0;
  return itr;
}

uint64_t erw_alloc(erw_state_t* erw, enum erw_alloc_category category, uint32_t alignment, uint64_t size) {
  if (!alignment) alignment = 1;
  erw_alloc_bucket_t* b = find_alloc_bucket(&erw->alloc_buckets, category, alignment);
  uint64_t result = (b->current + alignment - 1) &- (uint64_t)alignment;
  if ((b->current = result + size) > b->end) {
    erw->retry = true;
    result = 0;
  }
  return result;
}

uint8_t* erw_view_v(erw_state_t* erw, uint64_t v) {
  uint64_t v2f = v2f_map_lookup(&erw->v2f, v)->v2f;
  if (v2f & V2F_SPECIAL) return NULL;
  return erw->f + (v + v2f);
}

// misc

uint64_t erw_set_entry(erw_state_t* erw, uint64_t new_entry) {
  return call_erwNNE_(set_entry, erw, new_entry);
}

// phdrs

void erw_phdrs_add_gnu_eh_frame(erw_state_t* erw, uint64_t v, uint64_t sz) {
  call_erwNNE_(phdrs_add_gnu_eh_frame, erw, v, sz);
}

void erw_phdrs_set_interpreter(erw_state_t* erw, const char* value) {
  call_erwNNE_(phdrs_set_interpreter, erw, value);
}

void erw_phdrs_set_stack_prot(erw_state_t* erw, uint32_t prot) {
  call_erwNNE_(phdrs_set_stack_prot, erw, prot);
}

void erw_phdrs_remove(erw_state_t* erw, uint32_t tag) {
  call_erwNN_(phdrs_remove, erw, tag);
}

void* erw_phdrs_find_first(erw_state_t* erw, uint32_t tag) {
  return call_erwNN_(phdrs_find_first, erw, tag);
}

bool erw_phdrs_is_entry_callable(erw_state_t* erw) {
  return call_erwNNE_(phdrs_is_entry_callable, erw);
}

// dhdrs

void erw_dhdrs_init(erw_state_t* erw) {
  if (erw->dhdrs.base) return;
  call_erwNNE_(dhdrs_init, erw);
}

static void erw_dhdrs_remove(erw_state_t* erw, uint32_t tag) {
  if (!erw->dhdrs.base) erw_dhdrs_init(erw);
  call_erwNN_(dhdrs_remove, erw, tag);
}

void erw_dhdrs_remove_mask(erw_state_t* erw, uint64_t mask) {
  if (!erw->dhdrs.base) erw_dhdrs_init(erw);
  call_erwNNE_(dhdrs_remove_mask, erw, mask);
}

void erw_dhdrs_set_u(erw_state_t* erw, uint32_t tag, uint64_t value) {
  if (!erw->dhdrs.base) erw_dhdrs_init(erw);
  call_erwNNE_(dhdrs_set_u, erw, tag, value);
}

uint64_t erw_dhdrs_get_flags(erw_state_t* erw) {
  if (!erw->dhdrs.base) erw_dhdrs_init(erw);
  return call_erwNNE_(dhdrs_get_flags, erw);
}

void erw_dhdrs_add_remove_flags(erw_state_t* erw, uint32_t tag, uint32_t add, uint32_t rem) {
  if (!erw->dhdrs.base) erw_dhdrs_init(erw);
  call_erwNNE_(dhdrs_add_remove_flags, erw, tag, add, rem);
}

void erw_dhdrs_set_str(erw_state_t* erw, uint32_t tag, const char* str) {
  if (!erw->dhdrs.base) erw_dhdrs_init(erw);
  call_erwNNE_(dhdrs_set_str, erw, tag, str);
}

bool erw_dhdrs_has(erw_state_t* erw, uint32_t tag, uint64_t* value) {
  if (!erw->dhdrs.base) erw_dhdrs_init(erw);
  return call_erwNNE_(dhdrs_has, erw, tag, value);
}

bool erw_dhdrs_has_needed(erw_state_t* erw, const char* str) {
  if (!erw->dhdrs.base) erw_dhdrs_init(erw);
  return call_erwNNE_(dhdrs_has_needed, erw, str);
}

void erw_dhdrs_add_early_needed(erw_state_t* erw, const char* str) {
  uint64_t encoded = erw_dynstr_encode(erw, str);
  call_erwNNE_(dhdrs_add_early_u, erw, DT_NEEDED, encoded);
}

void erw_dhdrs_add_late_needed(erw_state_t* erw, const char* str) {
  uint64_t encoded = erw_dynstr_encode(erw, str);
  call_erwNNE_(dhdrs_add_late_u, erw, DT_NEEDED, encoded);
}

void erw_dhdrs_remove_needed(erw_state_t* erw, const char* str) {
  if (!erw->dhdrs.base) erw_dhdrs_init(erw);
  call_erwNNE_(dhdrs_remove_needed, erw, str);
}

// relocs

void erw_relocs_init(erw_state_t* erw) {
  if (erw->relocs[0].base) return;
  if (!erw->dhdrs.base) erw_dhdrs_init(erw);
  reloc_editor_t* dst = call_erwNNE_(relocs_init, erw);
  for (; dst < erw->relocs + MAX_RELOC_EDITORS; ++dst) {
    memset(dst, 0, sizeof(*dst));
  }
  if (!erw->relocs[0].base) erw->relocs[0].base = (void*)erw->f;
}

void erw_relocs_add(erw_state_t* erw, uint32_t section, uint32_t kind, uint64_t where, uint32_t sym, uint64_t explicit_addend) {
  if (!erw->relocs[0].base) erw_relocs_init(erw);
  if (!call_erwNNE_(relocs_add, erw, section, kind, where, sym, explicit_addend)) {
    FATAL("Too many reloc editors");
  }
}

void erw_relocs_add_early(erw_state_t* erw, uint32_t section, uint32_t kind, uint64_t where, uint32_t sym, uint64_t explicit_addend) {
  if (!erw->relocs[0].base) erw_relocs_init(erw);
  if (!call_erwNNE_(relocs_add_early, erw, section, kind, where, sym, explicit_addend)) {
    FATAL("Too many reloc editors");
  }
}

void erw_relocs_expand_relr(erw_state_t* erw) {
  if (!erw->relocs[0].base) erw_relocs_init(erw);
  if (!call_erwNNE_(relocs_expand_relr, erw)) {
    FATAL("Do not know how to expand DT_RELR for machine type %u", erw->machine);
  }
}

// dsyms

bool erw_dhdrs_is_eager(erw_state_t* erw) {
  return (erw_dhdrs_get_flags(erw) & (DF_BIND_NOW | ((uint64_t)DF_1_NOW << 32))) != 0;
}

static void set_sym_info_if(erw_state_t* erw, uint32_t to_set, uint32_t if_set) {
  sym_info_t* itr = erw->dsyms.info;
  sym_info_t* end = itr + erw->dsyms.count;
  for (; itr < end; ++itr) {
    uint32_t flags = itr->flags;
    if (flags & if_set) {
      itr->flags = flags | to_set;
    }
  }
}

void erw_dsyms_add_hash(erw_state_t* erw, uint32_t tag) {
  if (!erw->dsyms.base) erw_dsyms_init(erw);
  switch (tag) {
  case DT_HASH:
    if (erw->dsyms.want_hash) return;
    erw->dsyms.want_hash = true;
    erw->modified |= ERW_MODIFIED_DSYMS;
    set_sym_info_if(erw, SYM_INFO_IN_HASH, erw->dsyms.want_gnu_hash ? SYM_INFO_IN_GNU_HASH : SYM_INFO_EXPORTABLE);
    break;
  case DT_GNU_HASH:
    if (erw->dsyms.want_gnu_hash) return;
    erw->dsyms.want_gnu_hash = true;
    erw->modified |= ERW_MODIFIED_DSYMS;
    set_sym_info_if(erw, SYM_INFO_IN_GNU_HASH, erw->dsyms.want_hash ? SYM_INFO_IN_HASH : SYM_INFO_EXPORTABLE);
    break;
  }
}

void erw_dsyms_ensure_versions_array(erw_state_t* erw) {
  if (!erw->dsyms.base) erw_dsyms_init(erw);
  if (erw->dsyms.versions) return;
  uint32_t capacity = erw->dsyms.capacity;
  uint32_t count = erw->dsyms.count;
  if (capacity) {
    erw->dsyms.versions = (uint16_t*)malloc(capacity * sizeof(uint16_t));
  } else if (count) {
    size_t esz = erw->file_kind & ERW_FILE_KIND_64 ? sizeof(struct Elf64_Sym) : sizeof(struct Elf32_Sym);
    void* syms = malloc(count * esz);
    memcpy(syms, erw->dsyms.base, count * esz);
    erw->dsyms.base = syms;
    erw->dsyms.capacity = count;
    erw->dsyms.versions = (uint16_t*)malloc(count * sizeof(uint16_t));
  } else {
    return;
  }
  uint32_t i;
  uint16_t* vers = erw->dsyms.versions;
  sym_info_t* info = erw->dsyms.info;
  for (i = 0; i < count; ++i) {
    vers[i] = (info[i].flags & SYM_INFO_LOCAL) ? 0 : 1;
  }
}

void erw_dsyms_init(erw_state_t* erw) {
  if (erw->dsyms.base) return;
  if (!erw->dhdrs.base) erw_dhdrs_init(erw);
  call_erwNNE_(dsyms_init, erw);
}

void erw_dsyms_clear_version(erw_state_t* erw, sht_t* sym_names) {
  if (!erw->dsyms.base) erw_dsyms_init(erw);
  if (!erw->dsyms.versions) return;
  if (!erw->vers.count) erw_vers_init(erw);
  call_erwNNE_(dsyms_clear_version, erw, sym_names);
}

uint32_t erw_dsyms_find_or_add(erw_state_t* erw, const char* name, uint16_t veridx, uint8_t stt) {
  return call_erwNNE_(dsyms_find_or_add, erw, name, veridx, stt);
}

// vers

static void vers_ensure_vaddrs(erw_state_t* erw, ver_group_t* groups, size_t nb_group, size_t nb_item, const char* section_name) {
  // TODO: is there a nicer and more general way of doing this?
  ver_group_t* g;
  int32_t g_free = 0;
  uint32_t g_count = 0;
  size_t nb_required = 0;
  bool inplace = true;
  bool g_alloc_required = false;
  uint64_t* vs = NULL;
  uint32_t vs_size = 0;
  for (g = groups; g; g = g->next) {
    ++g_count;
    if (g->state & VER_EDIT_STATE_PENDING_DELETE) {
      if (g->vaddr) {
        g_free += 1;
      }
    } else {
      nb_required += nb_group;
      if (!g->vaddr) {
        g_alloc_required = true;
        g_free -= 1;
      }
      int32_t i_free = 0;
      uint32_t i_count = 0;
      ver_item_t* i = g->items;
      bool i_alloc_required = false;
      for (; i; i = i->next) {
        ++i_count;
        if (i->state & VER_EDIT_STATE_PENDING_DELETE) {
          if (i->vaddr) {
            i_free += 1;
          }
        } else {
          nb_required += nb_item;
          if (!i->vaddr) {
            i_alloc_required = true;
            i_free -= 1;
          }
        }
      }
      if (i_free < 0) {
        inplace = false;
      } else if (i_alloc_required && inplace) {
        if (i_count > vs_size) {
          free(vs);
          vs = malloc((vs_size = i_count) * sizeof(uint64_t));
        }
        i_count = 0;
        for (i = g->items; i; i = i->next) {
          if (i->vaddr) vs[i_count++] = i->vaddr;
        }
        i_count = 0;
        for (i = g->items; i; i = i->next) {
          if (i->state & VER_EDIT_STATE_PENDING_DELETE) {
            i->vaddr = 0;
          } else {
            i->vaddr = vs[i_count++];
            i->state |= VER_EDIT_STATE_DIRTY;
          }
        }
      }
    }
  }
  if (g_free < 0) {
    inplace = false;
  } else if (g_alloc_required && inplace) {
    if (g_count > vs_size) {
      free(vs);
      vs = malloc((vs_size = g_count) * sizeof(uint64_t));
    }
    g_count = 0;
    for (g = groups; g; g = g->next) {
      if (g->vaddr) vs[g_count++] = g->vaddr;
    }
    g_count = 0;
    for (g = groups; g; g = g->next) {
      if (g->state & VER_EDIT_STATE_PENDING_DELETE) {
        g->vaddr = 0;
      } else {
        g->vaddr = vs[g_count++];
        g->state |= VER_EDIT_STATE_DIRTY;
      }
    }
  }
  free(vs);
  if (inplace) {
    if (!erw->retry && g_count) {
      call_erwNNE_(vers_update_shdr_light, erw, section_name, g_count);
    }
    return;
  }
  uint64_t v = erw_alloc(erw, erw_alloc_category_v_r, 4, nb_required);
  if (!erw->retry) {
    call_erwNNE_(vers_update_shdr_full, erw, section_name, v, nb_required, g_count);
    uint64_t v_item = v + g_count * nb_group;
    for (g = groups; g; g = g->next) {
      if (g->state & VER_EDIT_STATE_PENDING_DELETE) continue;
      g->vaddr = v;
      g->state |= VER_EDIT_STATE_DIRTY;
      v += nb_group;
      ver_item_t* i = g->items;
      for (; i; i = i->next) {
        if (i->state & VER_EDIT_STATE_PENDING_DELETE) continue;
        i->vaddr = v_item;
        i->state |= VER_EDIT_STATE_DIRTY;
        v_item += nb_item;
      }
    }
  }
}

ver_index_ref_t* erw_vers_ensure_index(erw_state_t* erw, uint32_t idx) {
  if (idx >= erw->vers.count) {
    if (idx >= erw->vers.capacity) {
      uint32_t new_capacity = erw->vers.capacity + (idx < erw->vers.capacity ? erw->vers.capacity : idx + 1);
      erw->vers.base = (ver_index_ref_t*)realloc(erw->vers.base, new_capacity * sizeof(*erw->vers.base));
      erw->vers.capacity = new_capacity;
    }
    memset(erw->vers.base + erw->vers.count, 0, (idx + 1 - erw->vers.count) * sizeof(*erw->vers.base));
    erw->vers.count = idx + 1;
  }
  return erw->vers.base + idx;
}

void erw_vers_init(erw_state_t* erw) {
  if (erw->vers.count) return;
  if (!erw->dhdrs.base) erw_dhdrs_init(erw);
  erw_vers_ensure_index(erw, 1);
  call_erwNNE_(vers_init, erw);
}

static bool ver_equal(erw_state_t* erw, ver_index_ref_t* ver, const char* lib_name, const char* ver_name) {
  ver_group_t* g = ver->group;
  if (lib_name) {
    if (!g) return false;
    if (strcmp(lib_name, erw_dynstr_decode(erw, g->name)) != 0) return false;
  } else {
    if (g) return false;
  }
  ver_item_t* i = ver->item;
  if (!i) return false;
  return strcmp(ver_name, erw_dynstr_decode(erw, i->str)) == 0;
}

static ver_group_t* ensure_verneed(erw_state_t* erw, const char* lib_name) {
  ver_group_t** chain = &erw->vers.need;
  ver_group_t* g = *chain;
  for (; g; chain = &g->next, g = g->next) {
    if (strcmp(lib_name, erw_dynstr_decode(erw, g->name)) == 0) {
      return g;
    }
  }
  g = (ver_group_t*)arena_alloc(&erw->arena, sizeof(ver_group_t));
  g->next = NULL;
  g->items = NULL;
  g->vaddr = 0;
  g->name = erw_dynstr_encode(erw, lib_name);
  g->state = VER_EDIT_STATE_DIRTY;
  *chain = g;
  return g;
}

static void add_vernaux(erw_state_t* erw, ver_group_t* g, const char* ver_name, uint32_t index) {
  ver_item_t* i = (ver_item_t*)arena_alloc(&erw->arena, sizeof(ver_item_t));
  i->vaddr = 0;
  i->str = erw_dynstr_encode(erw, ver_name);
  i->hash = erw_elf_hash(ver_name);
  i->flags = 0;
  i->index = index;
  i->state = VER_EDIT_STATE_DIRTY;
  ver_index_ref_t* r = erw_vers_ensure_index(erw, index & 0x7fff);
  r->group = g;
  r->item = i;
  i->next = g->items;
  g->items = i;
}

static void add_verdef(erw_state_t* erw, const char* ver_name, uint32_t index) {
  ver_group_t* g = (ver_group_t*)arena_alloc(&erw->arena, sizeof(ver_group_t) + sizeof(ver_item_t));
  ver_item_t* i = g->items = (ver_item_t*)(g + 1);
  g->vaddr = 0;
  g->name = 0;
  g->state = VER_EDIT_STATE_DIRTY;
  i->next = NULL;
  i->vaddr = 0;
  i->str = erw_dynstr_encode(erw, ver_name);
  i->hash = erw_elf_hash(ver_name);
  i->flags = 0;
  i->index = index;
  i->state = VER_EDIT_STATE_DIRTY;
  ver_index_ref_t* r = erw_vers_ensure_index(erw, index & 0x7fff);
  r->group = NULL;
  r->item = i;
  g->next = erw->vers.def;
  erw->vers.def = g;
}

uint16_t erw_vers_find_or_add(erw_state_t* erw, uint16_t* cache, const char* lib_name, const char* ver_name) {
  if (!ver_name) {
    return 1;
  }
  uint32_t n = erw->vers.count;
  if (n == 0) {
    erw_vers_init(erw);
    n = erw->vers.count;
  }
  ver_index_ref_t* base = erw->vers.base;
  uint32_t i = *cache;
  if (i < n && ver_equal(erw, base + i, lib_name, ver_name)) {
    return i;
  }
  uint32_t avail = n >= 3 ? n : 3;
  for (i = 0; i < n; ++i, ++base) {
    if (!base->group && !base->item) {
      if (i >= 3) avail = i;
    } else if (ver_equal(erw, base, lib_name, ver_name)) {
      *cache = i;
      return i;
    }
  }
  *cache = avail;
  if (lib_name) {
    erw->modified |= ERW_MODIFIED_VERNEED;
    add_vernaux(erw, ensure_verneed(erw, lib_name), ver_name, avail);
  } else {
    erw->modified |= ERW_MODIFIED_VERDEF;
    add_verdef(erw, ver_name, avail);
  }
  return avail;
}

static void verneed_removing_glibc_2_36(erw_state_t* erw) {
  // Prior to GLIBC_2.36, the addend on R_X86_64_JUMP_SLOT was honoured.
  // Since GLIBC_2.36, the addend on R_X86_64_JUMP_SLOT is ignored.
  // Subsequently, GLIBC_2.39 repurposed the addend field of R_X86_64_JUMP_SLOT
  // to contain information required by PLT rewriting. If any such repurposed
  // addends are present, the linker adds a dependency on libc::GLIBC_2.36.
  //
  // Therefore, if removing a GLIBC_2.36 dependency, R_X86_64_JUMP_SLOT addends
  // need changing back to zero, and PLT rewriting needs to be disabled.
  if (erw->machine == EM_X86_64) {
    call_erwNNE_(relocs_clear_rela_addends_for_type, erw, R_X86_64_JUMP_SLOT);
    erw_dhdrs_remove(erw, DT_X86_64_PLTENT);
  }
}

static void verneed_removing_glibc_abi_dt_relr(erw_state_t* erw) {
  if (erw_dhdrs_has(erw, DT_RELR, NULL)) {
    uint64_t dt_relrsz_value = 0;
    if (erw_dhdrs_has(erw, DT_RELRSZ, &dt_relrsz_value) && dt_relrsz_value) {
      erw_relocs_expand_relr(erw);
    }
  }
  erw_dhdrs_remove_mask(erw, (1ull << DT_RELR) | (1ull << DT_RELRSZ) | (1ull << DT_RELRENT));
}

static void verneed_removing(erw_state_t* erw, ver_group_t* g, ver_item_t* i) {
  const char* i_str = erw_dynstr_decode(erw, i->str);
  if (strcmp(i_str, "GLIBC_2.36") == 0) {
    if (strstr(erw_dynstr_decode(erw, g->name), "libc")) {
      verneed_removing_glibc_2_36(erw);
    }
  } else if (strcmp(i_str, "GLIBC_ABI_DT_RELR") == 0) {
    if (strstr(erw_dynstr_decode(erw, g->name), "libc")) {
      verneed_removing_glibc_abi_dt_relr(erw);
    }
  }
}

// dynstr

static void erw_dynstr_init(erw_state_t* erw) {
  if (erw->dynstr.state != dynstr_edit_state_uninit) return;
  if (!erw->dhdrs.base) erw_dhdrs_init(erw);
  erw->dynstr.state = dynstr_edit_state_read_only;
  call_erwNNE_(dynstr_init, erw);
}

const char* erw_dynstr_decode(erw_state_t* erw, uint64_t value) {
  switch (erw->dynstr.state) {
  case dynstr_edit_state_uninit:
    erw_dynstr_init(erw);
    break;
  case dynstr_edit_state_encode_building_new:
    return erw->dynstr.to_add + value;
  default:
    break;
  }
  uint64_t addr = erw->dynstr.original_base + value;
  v2f_entry_t* e = v2f_map_lookup(&erw->v2f, addr);
  if (e->v2f & V2F_SPECIAL) return "";
  return (const char*)erw->f + (addr + e->v2f);
}

uint32_t erw_elf_hash(const char* str) {
  uint32_t hash = 0;
  unsigned char c;
  while ((c = *str++)) {
    hash = (hash << 4) + c;
    hash ^= (hash & 0xf0000000) >> 24;
  }
  return hash & 0x0fffffff;
}

uint32_t erw_dynstr_decode_elf_hash(erw_state_t* erw, uint64_t value) {
  return erw_elf_hash(erw_dynstr_decode(erw, value));
}

uint32_t erw_gnu_hash(const char* str) {
  uint32_t hash = 5381;
  unsigned char c;
  while ((c = *str++)) {
    hash = (hash << 5) + hash + c;
  }
  return hash;
}

uint32_t erw_dynstr_decode_gnu_hash(erw_state_t* erw, uint64_t value) {
  return erw_gnu_hash(erw_dynstr_decode(erw, value));
}

static uint64_t dynstr_len_hash(const char* str) {
  uint32_t hash = 5381u;
  uint32_t len = 0u;
  uint8_t c;
  while ((c = str[len++])) {
    hash = hash * 33u + c;
  }
  return len + ((uint64_t)hash << 32);
}

static void erw_dynstr_rehash(erw_state_t* erw) {
  dynstr_hash_entry_t* old_table = erw->dynstr.table;
  uint32_t old_mask = erw->dynstr.table_mask;
  uint32_t new_mask = old_mask * 2 + 1;
  dynstr_hash_entry_t* new_table = (dynstr_hash_entry_t*)calloc(new_mask + 1ull, sizeof(dynstr_hash_entry_t));
  dynstr_hash_entry_t* old_end = old_table + old_mask + 1;
  for (; old_table < old_end; ++old_table) {
    if (old_table->len_hash) {
      uint32_t idx = (old_table->len_hash >> 32);
      for (;;) {
        dynstr_hash_entry_t* e = new_table + (idx++ & new_mask);
        if (!e->len_hash) {
          e->len_hash = old_table->len_hash;
          e->encoded = old_table->encoded;
          break;
        }
      }
    }
  }
  free(erw->dynstr.table);
  erw->dynstr.table = new_table;
  erw->dynstr.table_mask = new_mask;
}

static uint64_t add_existing_dynstr_to_table(void* ctx, uint64_t ref) {
  erw_state_t* erw = ctx;
  const char* str = erw_dynstr_decode(erw, ref);
  uint64_t len_hash = dynstr_len_hash(str);
  dynstr_hash_entry_t* table = erw->dynstr.table;
  uint32_t mask = erw->dynstr.table_mask;
  uint32_t idx = (len_hash >> 32);
  for (;;) {
    dynstr_hash_entry_t* e = table + (idx++ & mask);
    if (e->encoded == ref) {
      break;
    } else if (e->len_hash == 0) {
      e->len_hash = len_hash;
      e->encoded = ref;
      if (++erw->dynstr.table_count * 4ull >= mask * 3ull) {
        erw_dynstr_rehash(erw);
      }
      break;
    }
  }
  return ref;
}

static void erw_dynstr_build_table(erw_state_t* erw) {
  if (erw->dynstr.table_mask) {
    erw->dynstr.table_count = 0;
    memset(erw->dynstr.table, 0, (erw->dynstr.table_mask + 1ull) * sizeof(*erw->dynstr.table));
  } else {
    erw->dynstr.table_mask = 15;
    erw->dynstr.table = calloc(erw->dynstr.table_mask + 1ull, sizeof(*erw->dynstr.table));
  }
  call_erwNNE_(dynstr_visit_refs, erw, erw, add_existing_dynstr_to_table);
  erw->dynstr.state = dynstr_edit_state_encode_from_existing;
}

static int cmp_dynstr_hash_entry_t_encoded(const void* lhs, const void* rhs) {
  const dynstr_hash_entry_t* le = (const dynstr_hash_entry_t*)lhs;
  const dynstr_hash_entry_t* re = (const dynstr_hash_entry_t*)rhs;
  if (le->encoded != re->encoded) {
    return le->encoded < re->encoded ? -1 : 1;
  }
  if ((uint32_t)le->len_hash != (uint32_t)re->len_hash) {
    return (uint32_t)le->len_hash < (uint32_t)re->len_hash ? 1 : -1;
  }
  return 0;
}

static uint64_t apply_dynstr_remap(void* ctx, uint64_t ref) {
  dynstr_hash_entry_t* remap = ctx;
  uint32_t n = remap[-1].len_hash;
  while (n > 1) {
    uint32_t mid = n / 2;
    if (ref < remap[mid].encoded) {
      n = mid;
    } else {
      remap += mid;
      n -= mid;
    }
  }
  return ref + remap->len_hash;
}

static void erw_dynstr_start_building_new(erw_state_t* erw) {
  dynstr_hash_entry_t* itr = erw->dynstr.table;
  dynstr_hash_entry_t* end = itr + erw->dynstr.table_mask;
  uint64_t max_off = 0;
  uint64_t sum_len = 0;
  for (; itr <= end; ++itr) {
    uint64_t x = itr->len_hash;
    if (x) {
      x = (uint32_t)x;
      sum_len += x;
      x += itr->encoded;
      if (x > max_off) {
        max_off = x;
      }
    }
  }
  if (max_off == 0) {
    // No strings yet; start with an empty table.
    erw->dynstr.to_add_size = 1;
    if (erw->dynstr.to_add_size > erw->dynstr.to_add_capacity) {
      free(erw->dynstr.to_add);
      erw->dynstr.to_add_capacity = erw->dynstr.to_add_size + 32;
      erw->dynstr.to_add = malloc(erw->dynstr.to_add_capacity);
    }
    erw->dynstr.to_add[0] = '\0';
  } else if (max_off <= (sum_len + (sum_len >> 3)) || max_off <= 1024) {
    // Strings are sufficiently dense to use as-is.
    erw->dynstr.to_add_size = max_off;
    if (erw->dynstr.to_add_size > erw->dynstr.to_add_capacity) {
      free(erw->dynstr.to_add);
      erw->dynstr.to_add_capacity = erw->dynstr.to_add_size + 32;
      erw->dynstr.to_add = malloc(erw->dynstr.to_add_capacity);
    }
    materialise_v_range_to(erw, erw->dynstr.original_base, max_off, erw->dynstr.to_add);
  } else {
    // Strings are somewhat sparse; compactify them.
    if (sum_len > erw->dynstr.to_add_capacity) {
      free(erw->dynstr.to_add);
      erw->dynstr.to_add_capacity = sum_len + 32;
      erw->dynstr.to_add = malloc(erw->dynstr.to_add_capacity);
    }
    dynstr_hash_entry_t* remap = (dynstr_hash_entry_t*)malloc(sizeof(dynstr_hash_entry_t) * (erw->dynstr.table_count + 1ull)) + 1;
    dynstr_hash_entry_t* dst = remap;
    for (itr = erw->dynstr.table; itr <= end; ++itr) {
      uint64_t len_hash = itr->len_hash;
      if (len_hash) {
        dst->encoded = itr->encoded;
        dst->len_hash = len_hash;
        ++dst;
      }
    }
    qsort(remap, dst - remap, sizeof(*remap), cmp_dynstr_hash_entry_t_encoded);
    dynstr_hash_entry_t* out = remap;
    uint64_t recoded_up_to = 0;
    uint32_t out_length = 0;
    for (itr = remap; itr < dst; ++itr) {
      uint64_t encoded = itr->encoded;
      if (encoded < recoded_up_to) continue;
      uint32_t len = (uint32_t)itr->len_hash + 1;
      materialise_v_range_to(erw, erw->dynstr.original_base + encoded, len, erw->dynstr.to_add + out_length);
      uint64_t delta = (uint64_t)out_length - encoded;
      out_length += len;
      recoded_up_to = encoded + len;
      if (out == remap || out[-1].len_hash != delta) {
        out->encoded = encoded;
        out->len_hash = delta;
        ++out;
      }
    }
    erw->dynstr.to_add_size = out_length;
    remap[-1].len_hash = (uint64_t)(out - remap);
    for (itr = erw->dynstr.table; itr <= end; ++itr) {
      if (itr->len_hash) {
        itr->encoded = apply_dynstr_remap(remap, itr->encoded);
      }
    }
    call_erwNNE_(dynstr_visit_refs, erw, remap, apply_dynstr_remap);
    free(remap - 1);
  }
  erw->dynstr.state = dynstr_edit_state_encode_building_new;
  erw->modified |= ERW_MODIFIED_DYNSTR;
}

uint64_t erw_dynstr_encode(erw_state_t* erw, const char* str) {
  switch (erw->dynstr.state) {
  case dynstr_edit_state_uninit:
    erw_dynstr_init(erw);
    // fallthrough
  case dynstr_edit_state_read_only:
    erw_dynstr_build_table(erw);
    break;
  default:
    break;
  }

  // We might already have the string.
  uint64_t len_hash = dynstr_len_hash(str);
  dynstr_hash_entry_t* table = erw->dynstr.table;
  uint32_t mask = erw->dynstr.table_mask;
  uint32_t idx = (len_hash >> 32);
  dynstr_hash_entry_t* e;
  for (;;) {
    e = table + (idx++ & mask);
    if (e->len_hash == len_hash) {
      const char* existing = erw_dynstr_decode(erw, e->encoded);
      if (memcmp(existing, str, (uint32_t)len_hash) == 0) {
        return e->encoded;
      }
    } else if (e->len_hash == 0) {
      break;
    }
  }

  if (erw->dynstr.state != dynstr_edit_state_encode_building_new) {
    erw_dynstr_start_building_new(erw);
  }

  if ((erw->dynstr.to_add_capacity - erw->dynstr.to_add_size) < (uint32_t)len_hash) {
    uint32_t new_capacity = erw->dynstr.to_add_capacity + ((uint32_t)len_hash < erw->dynstr.to_add_capacity ? erw->dynstr.to_add_capacity : (uint32_t)len_hash);
    erw->dynstr.to_add = realloc(erw->dynstr.to_add, new_capacity);
    erw->dynstr.to_add_capacity = new_capacity;
  }

  uint64_t result = erw->dynstr.to_add_size;
  e->len_hash = len_hash;
  e->encoded = result;
  erw->dynstr.to_add_size = result + (uint32_t)len_hash;
  memcpy(erw->dynstr.to_add + result, str, (uint32_t)len_hash);
  if (++erw->dynstr.table_count * 4ull >= mask * 3ull) {
    erw_dynstr_rehash(erw);
  }
  return result;
}

// etc.

static bool erw_init_common(erw_state_t* erw, const char** err_reason) {
  // Validate the ELF header.
  if (erw->f_size < sizeof(struct Elf32_Ehdr)) {
    *err_reason = "too small to be an ELF file";
    return false;
  }
  struct Elf32_Ehdr* hdr32 = (struct Elf32_Ehdr*)erw->f;
  if (memcmp(hdr32->e_ident, "\177ELF", 4) != 0) {
    *err_reason = "not an ELF file";
    return false;
  }
  // In theory e_ident[5] tells us the endianness of the file,
  // but the kernel ignores this field, so we ignore it too, and
  // infer endianness from e_type (as part of validating e_type).
  uint16_t type = hdr32->e_type;
  bool swap = false;
  if (type == ET_EXEC || type == ET_DYN) {
    // ok
  } else {
    type = __builtin_bswap16(type);
    if (type == ET_EXEC || type == ET_DYN) {
      // ok, need bswap
      swap = true;
      erw->file_kind |= ERW_FILE_KIND_BSWAP;
    } else {
      *err_reason = "unsupported type of ELF file";
      return false;
    }
  }
  // In theory e_ident[4] tells us the 32/64-bit-ness of the file,
  // but the kernel ignores this field, so we consider it a hint
  // rather than authoritative, and infer 32/64-bit-ness from
  // e_phentsize (as part of validating e_phentsize).
  uint8_t clazz = hdr32->e_ident[4];
  if (clazz != 2) {
    uint16_t e_phentsize = hdr32->e_phentsize;
    if (swap) e_phentsize = __builtin_bswap16(e_phentsize);
    if (e_phentsize == sizeof(struct Elf32_Phdr)) {
      goto good_phentsize;
    }
  }
  if (erw->f_size >= sizeof(struct Elf64_Ehdr)) {
    uint16_t e_phentsize = ((struct Elf64_Ehdr*)hdr32)->e_phentsize;
    if (swap) e_phentsize = __builtin_bswap16(e_phentsize);
    if (e_phentsize == sizeof(struct Elf64_Phdr)) {
      erw->file_kind |= ERW_FILE_KIND_64;
      goto good_phentsize;
    }
  }
  if (clazz == 2) {
    uint16_t e_phentsize = hdr32->e_phentsize;
    if (swap) e_phentsize = __builtin_bswap16(e_phentsize);
    if (e_phentsize == sizeof(struct Elf32_Phdr)) {
      goto good_phentsize;
    }
  }
  *err_reason = "unsupported e_phentsize value in ELF header";
  return false;
good_phentsize:

  // Initialise the phdr editor now.
  call_erwNNE_(phdrs_init, erw);
  return true;
}

bool erw_init_from_mem(erw_state_t* erw, const void* mem, size_t len, uint64_t guest_page_size, const char** err_reason) {
  const char* sink;
  if (!err_reason) err_reason = &sink;
  *err_reason = NULL;

  memset(erw, 0, sizeof(*erw));
  erw->guest_page_size = guest_page_size;
  erw->f_size = len;

  erw->f = malloc(len);
  erw->orig_mem = malloc(len);
  if (!erw->f || !erw->orig_mem) {
    *err_reason = "out of memory";
    return false;
  }

  memcpy(erw->f, mem, len);
  memcpy(erw->orig_mem, mem, len);
  return erw_init_common(erw, err_reason);
}

bool erw_init_from_fd(erw_state_t* erw, int fd, uint64_t guest_page_size, const char** err_reason) {
  const char* sink;
  if (!err_reason) err_reason = &sink;
  *err_reason = NULL;

  memset(erw, 0, sizeof(*erw));
  erw->guest_page_size = guest_page_size;
  if (fd < 0) {
    *err_reason = "bad fd";
    return false;
  }

  // Read the file into memory.
  {
    struct stat st;
    if (fstat(fd, &st) == 0) erw->f_size = st.st_size;
  }
  if (!erw->f_size || !erw_do_mmap(erw, fd)) {
    ++erw->f_size;
    size_t capacity = erw->f_size < 512u ? 512u : erw->f_size;
    erw->f_size = 0;
    erw->f = malloc(capacity);
    if (!erw->f) {
      *err_reason = "out of memory";
      return false;
    }
    for (;;) {
      ssize_t n = read(fd, erw->f + erw->f_size, capacity - erw->f_size);
      if (n >= 0) {
        if (n == 0) break;
        erw->f_size += (size_t)n;
        if (erw->f_size >= capacity) {
          capacity += (capacity >> 1);
          if (erw->f_size >= capacity) {
            *err_reason = "file too big";
            return false;
          }
          void* new_f = realloc(erw->f, capacity);
          if (!new_f) {
            *err_reason = "out of memory";
            return false;
          }
          erw->f = new_f;
        }
      } else if (errno != EINTR) {
        *err_reason = "read error";
        return false;
      }
    }
    if (lseek(fd, 0, SEEK_SET) != 0) {
      // We've now read the file, but the file isn't rewindable, so we
      // need to take a copy of it now, in case we later modify it.
      void* orig_copy = malloc(erw->f_size);
      if (!orig_copy) {
        *err_reason = "out of memory";
        return false;
      }
      memcpy(orig_copy, erw->f, erw->f_size);
      erw->orig_mem = orig_copy;
    }
  }

  if (erw_init_common(erw, err_reason)) {
    erw->original->fd = fd;
    return true;
  } else {
    return false;
  }
}

bool erw_flush(erw_state_t* erw) {
  return call_erwNNE_(flush, erw);
}

void erw_free(erw_state_t* erw) {
  if (erw->phdrs.base && erw->phdrs.capacity) free(erw->phdrs.base);
  if (erw->vers.count) free(erw->vers.base);
  free_editors_common(erw);
  free(erw->dynstr.to_add);
  free(erw->dynstr.table);
  free(erw->alloc_buckets.base);
  free(erw->original);
  free(erw->v2f.entries);
  arena_free(&erw->arena);
  if (erw->file_kind & ERW_FILE_KIND_MMAPED) {
    munmap(erw->f, erw->mmap_size);
  } else {
    free(erw->f);
    free(erw->orig_mem);
  }
}
