// erw.h: Low-level ELF re-writing functionality.
#pragma once
#include <stdbool.h>
#include "elf.h"
#include "arena.h"
#include "v2f.h"

struct sht_t;

enum erw_alloc_category {
  erw_alloc_category_f,
  erw_alloc_category_v_r,
  erw_alloc_category_v_rx,
  erw_alloc_category_v_rw,
  erw_alloc_category_v_rw_zero,
};

typedef struct erw_alloc_bucket_t {
  enum erw_alloc_category category;
  uint32_t alignment;
  uint64_t start;
  uint64_t current;
  uint64_t end;
} erw_alloc_bucket_t;

typedef struct erw_alloc_buckets_t {
  erw_alloc_bucket_t* base;
  uint32_t count;
  uint32_t capacity;
  uint32_t category_mask;
} erw_alloc_buckets_t;

typedef struct phdr_editor_t {
  void* base; // NULL if uninitialised. Type is `struct Elf32_Phdr*` or `struct Elf64_Phdr*`. All fields therein file endian.
  uint32_t count;
  uint32_t capacity; // 0 if currently editing in-place
} phdr_editor_t;

typedef struct dhdr_editor_t {
  void* base; // NULL if uninitialised. Type is `struct Elf32_Dyn*` or `struct Elf64_Dyn*`. All fields therein file endian.
  uint32_t count;
  uint32_t capacity; // 0 if currently editing in-place
} dhdr_editor_t;

enum dynstr_edit_state {
  dynstr_edit_state_uninit,
  dynstr_edit_state_read_only,
  dynstr_edit_state_encode_from_existing,
  dynstr_edit_state_encode_building_new,
};

typedef struct dynstr_hash_entry_t {
  uint64_t len_hash; // len in low u32, hash in high u32. Length includes NUL terminator.
  uint64_t encoded;
} dynstr_hash_entry_t;

typedef struct dynstr_editor_t {
  uint64_t original_base;
  char* to_add;
  uint32_t to_add_size;
  uint32_t to_add_capacity;
  enum dynstr_edit_state state;
  dynstr_hash_entry_t* table;
  uint32_t table_count;
  uint32_t table_mask;
} dynstr_editor_t;

typedef struct sym_info_t {
  uint32_t flags; // SYM_INFO_*
  uint32_t hash_distance;
} sym_info_t;

#define SYM_INFO_RELOC_EAGER       0x0001 // Set by any relocation that is always non-lazy
#define SYM_INFO_RELOC_MAYBE_EAGER 0x0002 // Set by any relocation that is lazy or eager depending erw_dhdrs_is_eager
#define SYM_INFO_RELOC_COPY        0x0004 // Set by any reloc_kind_class_copy
#define SYM_INFO_RELOC_PLT         0x0008 // Set by any reloc_kind_class_plt
#define SYM_INFO_RELOC_REGULAR     0x0010 // Set by any other reloc_kind_
#define SYM_INFO_IN_HASH           0x0020 // DT_HASH
#define SYM_INFO_IN_GNU_HASH       0x0040 // DT_GNU_HASH
#define SYM_INFO_LOCAL             0x0080 // Set if STB_LOCAL or dl_symbol_visibility_binds_local_p. If set, then relocs don't do a symbol lookup, and hash tables skip over it.
#define SYM_INFO_EXPORTABLE        0x0100 // STT_ and STB_ and etc. compatible with export. If set, it _should_ be in hash table. If not set, hash tables skip over it.
#define SYM_INFO_PLT_EXPORTABLE    0x0200
#define SYM_INFO_WANT_POLYFILL     0x0400

typedef struct sym_editor_t {
  void* base; // NULL if uninitialised. Type is `struct Elf32_Sym*` or `struct Elf64_Sym*`. All fields therein file endian.
  uint16_t* versions; // NULL if not present. Values in file endian.
  sym_info_t* info; // Not part of ELF; maintained for benefit of editors.
  uint32_t count;
  uint32_t capacity; // 0 if currently editing in-place
  uint32_t original_gnu_hash_count;
  bool original_had_hash;
  bool original_had_gnu_hash;
  bool want_hash;
  bool want_gnu_hash;
} sym_editor_t;

#define VER_EDIT_STATE_DIRTY 0x01
#define VER_EDIT_STATE_PENDING_DELETE 0x02
#define VER_EDIT_STATE_REMOVED_REFS 0x04 // have removed at least one reference to this item; should check for remaining references then decide whether garbage or not

typedef struct ver_item_t {
  struct ver_item_t* next;
  uint64_t vaddr;
  uint32_t str;
  uint32_t hash;
  uint16_t flags;
  uint16_t index;
  uint8_t state; // VER_EDIT_STATE_
} ver_item_t;

typedef struct ver_group_t {
  struct ver_group_t* next;
  ver_item_t* items;
  uint64_t vaddr;
  uint32_t name; // filename for VerNeed, ignored for VerDef
  uint8_t state; // VER_EDIT_STATE_
} ver_group_t;

typedef struct ver_index_ref_t {
  ver_group_t* group; // NULL for verdef
  ver_item_t* item;
} ver_index_ref_t;

typedef struct ver_editor_t {
  ver_index_ref_t* base;
  uint32_t count; // 0 if uninitialised, otherwise >= 2
  uint32_t capacity;
  ver_group_t* def;
  ver_group_t* need;
} ver_editor_t;

typedef struct reloc_editor_t {
  void* base; // NULL if uninitialised
  uint64_t count;
  uint64_t capacity; // 0 if currently editing in-place
  uint8_t entry_size;
  uint8_t dt; // DT_REL or DT_RELA or DT_JMPREL or (DT_GNU_CONFLICT & 0xff)
  uint16_t flags; // RELOC_EDIT_*
} reloc_editor_t;

#define RELOC_EDIT_FLAG_EXPLICIT_ADDEND 0x01
#define RELOC_EDIT_FLAG_DIRTY 0x02
#define RELOC_EDIT_FLAG_NO_NOOPS 0x04

#define MAX_RELOC_EDITORS 5

typedef struct erw_original_state_t {
  uint64_t f_size;
  uint64_t phdr_count;
  int fd;
  /* Array of struct Elf32_Phdr/Elf64_Phdr at tail. */
} erw_original_state_t;

#define ERW_MODIFIED_MISC        0x001 // Any in-place edit, including ELF header itself
#define ERW_MODIFIED_PHDRS       0x002
#define ERW_MODIFIED_DHDRS       0x004
#define ERW_MODIFIED_DYNSTR      0x008
#define ERW_MODIFIED_DSYMS       0x010
#define ERW_MODIFIED_VERNEED     0x020
#define ERW_MODIFIED_VERDEF      0x040
#define ERW_MODIFIED_RELOCS      0x080
#define ERW_MODIFIED_HASH_TABLES 0x100
#define ERW_MODIFIED_SHDRS       0x200

#define ERW_FILE_KIND_BSWAP  0x01
#define ERW_FILE_KIND_64     0x02
#define ERW_FILE_KIND_MMAPED 0x04

typedef struct erw_state_t {
  uint8_t* f;
  uint64_t f_size;
  uint32_t modified; // ERW_MODIFIED_* flags
  bool retry;
  uint8_t file_kind; // ERW_FILE_KIND_* flags
  uint16_t machine;
  v2f_map_t v2f;
  phdr_editor_t phdrs;
  dhdr_editor_t dhdrs;
  dynstr_editor_t dynstr;
  sym_editor_t dsyms;
  ver_editor_t vers;
  reloc_editor_t relocs[MAX_RELOC_EDITORS];
  erw_alloc_buckets_t alloc_buckets;
  arena_t arena;
  erw_original_state_t* original;
  uint64_t guest_page_size;
  const char* filename; // For error messages.
  union {
    size_t mmap_size;
    void* orig_mem;
  };
} erw_state_t;

bool erw_init_from_fd(erw_state_t* erw, int fd, uint64_t guest_page_size, const char** err_reason);
bool erw_init_from_mem(erw_state_t* erw, const void* mem, size_t len, uint64_t guest_page_size, const char** err_reason);
bool erw_flush(erw_state_t* erw);
void erw_free(erw_state_t* erw);

uint64_t erw_alloc(erw_state_t* erw, enum erw_alloc_category category, uint32_t alignment, uint64_t size);
uint8_t* erw_view_v(erw_state_t* erw, uint64_t v);
uint64_t erw_set_entry(erw_state_t* erw, uint64_t new_entry);

void* erw_phdrs_find_first(erw_state_t* erw, uint32_t tag);
void erw_phdrs_add_gnu_eh_frame(erw_state_t* erw, uint64_t v, uint64_t sz);
void erw_phdrs_set_interpreter(erw_state_t* erw, const char* value);
void erw_phdrs_set_stack_prot(erw_state_t* erw, uint32_t prot);
void erw_phdrs_remove(erw_state_t* erw, uint32_t tag);
bool erw_phdrs_is_entry_callable(erw_state_t* erw);

void erw_dhdrs_init(erw_state_t* erw);
void erw_dhdrs_set_str(erw_state_t* erw, uint32_t tag, const char* str);
void erw_dhdrs_set_u(erw_state_t* erw, uint32_t tag, uint64_t value);
void erw_dhdrs_remove_mask(erw_state_t* erw, uint64_t mask);
void erw_dhdrs_add_remove_flags(erw_state_t* erw, uint32_t tag, uint32_t add, uint32_t rem);
bool erw_dhdrs_has(erw_state_t* erw, uint32_t tag, uint64_t* value);
bool erw_dhdrs_has_needed(erw_state_t* erw, const char* str);
void erw_dhdrs_add_early_needed(erw_state_t* erw, const char* str);
void erw_dhdrs_add_late_needed(erw_state_t* erw, const char* str);
void erw_dhdrs_remove_needed(erw_state_t* erw, const char* str);
bool erw_dhdrs_is_eager(erw_state_t* erw);
uint64_t erw_dhdrs_get_flags(erw_state_t* erw);

const char* erw_dynstr_decode(erw_state_t* erw, uint64_t value);
uint32_t erw_dynstr_decode_elf_hash(erw_state_t* erw, uint64_t value);
uint32_t erw_dynstr_decode_gnu_hash(erw_state_t* erw, uint64_t value);
uint64_t erw_dynstr_encode(erw_state_t* erw, const char* str);

void erw_dsyms_init(erw_state_t* erw);
void erw_dsyms_add_hash(erw_state_t* erw, uint32_t tag);
void erw_dsyms_clear_version(erw_state_t* erw, struct sht_t* sym_names);
void erw_dsyms_ensure_versions_array(erw_state_t* erw);
uint32_t erw_dsyms_find_or_add(erw_state_t* erw, const char* name, uint16_t veridx, uint8_t stt);
void erw32n_dsyms_sort_indices(erw_state_t* erw, uint32_t* indices, uint32_t count);
void erw64n_dsyms_sort_indices(erw_state_t* erw, uint32_t* indices, uint32_t count);
void erw32s_dsyms_sort_indices(erw_state_t* erw, uint32_t* indices, uint32_t count);
void erw64s_dsyms_sort_indices(erw_state_t* erw, uint32_t* indices, uint32_t count);

void erw_vers_init(erw_state_t* erw);
ver_index_ref_t* erw_vers_ensure_index(erw_state_t* erw, uint32_t idx);
uint16_t erw_vers_find_or_add(erw_state_t* erw, uint16_t* cache, const char* lib_name, const char* ver_name);

void erw_relocs_init(erw_state_t* erw);
void erw_relocs_add(erw_state_t* erw, uint32_t section, uint32_t kind, uint64_t where, uint32_t sym, uint64_t explicit_addend);
void erw_relocs_add_early(erw_state_t* erw, uint32_t section, uint32_t kind, uint64_t where, uint32_t sym, uint64_t explicit_addend);
void erw_relocs_expand_relr(erw_state_t* erw);

uint32_t erw_elf_hash(const char* str);
uint32_t erw_gnu_hash(const char* str);

#define FIRST(x, ...) (x)

#define call_erwNNE_(fn, ...) \
  (FIRST(__VA_ARGS__)->file_kind & ERW_FILE_KIND_64 ? \
    (FIRST(__VA_ARGS__)->file_kind & ERW_FILE_KIND_BSWAP ? erw64s_##fn(__VA_ARGS__) : erw64n_##fn(__VA_ARGS__)) : \
    (FIRST(__VA_ARGS__)->file_kind & ERW_FILE_KIND_BSWAP ? erw32s_##fn(__VA_ARGS__) : erw32n_##fn(__VA_ARGS__)))

#define call_erwNN_(fn, ...) \
  (FIRST(__VA_ARGS__)->file_kind & ERW_FILE_KIND_64 ? erw64_##fn(__VA_ARGS__) : erw32_##fn(__VA_ARGS__))

#define call_erwE_(fn, ...) \
  (FIRST(__VA_ARGS__)->file_kind & ERW_FILE_KIND_BSWAP ? erw_s_##fn(__VA_ARGS__) : erw_n_##fn(__VA_ARGS__))
