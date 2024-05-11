#include <stdlib.h>
#include <string.h>
#include "renamer.h"
#include "sht.h"
#include "erw.h"
#include "common.h"
#include "polyfiller.h"
#include "../build/x86_64/renames.h"

typedef struct u32_array_t {
  uint32_t* base;
  uint32_t count;
  uint32_t capacity;
} u32_array_t;

static void u32_array_increase_capacity(u32_array_t* arr) {
  uint32_t new_capacity = arr->capacity * 2;
  if (!new_capacity) new_capacity = 4;
  arr->capacity = new_capacity;
  arr->base = realloc(arr->base, new_capacity * sizeof(arr->base[0]));
}

typedef struct bespoke_registry_t {
  sht_t strings;
  sht_t renames;
} bespoke_registry_t;

static const char* find_colon_colon(const char* str) {
  for (;;) {
    str = strchr(str, ':');
    if (!str) break;
    if (str[1] == ':') return str;
    str += 2;
  }
  return NULL;
}

static void bespoke_registry_parse_name(bespoke_registry_t* rr, const char* name, uint32_t* dst) {
  const char* colon_colon = find_colon_colon(name);
  if (colon_colon) {
    dst[0] = sht_intern_u(&rr->strings, name, colon_colon - name);
    name = colon_colon + 2;
  } else {
    dst[0] = 0;
  }
  const char* at = strchr(name, '@');
  dst[1] = sht_intern_u(&rr->strings, name, at ? (size_t)(at - name) : strlen(name));
  if (at) {
    ++at;
    dst[2] = sht_intern_u(&rr->strings, at, strlen(at));
  } else {
    dst[2] = 0;
  }
}

static bool bespoke_registry_add_one(bespoke_registry_t* rr, const char* old_name, const char* new_name) {
  uint32_t old_parsed[3];
  bespoke_registry_parse_name(rr, old_name, old_parsed);
  uint32_t* new_parsed = (uint32_t*)sht_intern_p(&rr->renames, (const char*)(old_parsed + 1), sizeof(uint32_t) * 2);
  if (new_parsed[1]) {
    uint32_t sink[3];
    bespoke_registry_parse_name(rr, new_name, sink);
    return memcmp(new_parsed, sink, sizeof(sink)) == 0;
  } else {
    bespoke_registry_parse_name(rr, new_name, new_parsed);
    return true;
  }
}

static bool bespoke_registry_apply_to_name(bespoke_registry_t* rr, renamed_symbol_t* sym) {
  uint32_t key[2];
  if (sym->version) {
    if (!(key[1] = sht_lookup_u(&rr->strings, sym->version, strlen(sym->version)))) {
      return false;
    }
  } else {
    key[1] = 0;
  }
  if (!(key[0] = sht_lookup_u(&rr->strings, sym->name, strlen(sym->name)))) {
    return false;
  }
  uint32_t* value = (uint32_t*)sht_lookup_p(&rr->renames, (const char*)key, sizeof(key));
  if (!value) {
    return false;
  }
  uint32_t new_lib = value[0];
  if (new_lib) sym->lib = sht_u_key(&rr->strings, new_lib);
  sym->name = sht_u_key(&rr->strings, value[1]);
  uint32_t new_ver = value[2];
  sym->version = new_ver ? sht_u_key(&rr->strings, new_ver) : NULL;
  return true;
}

static void export_polyfill_symbol(renamer_t* renamer, const char* name, uint16_t* ver_idx);

static void bespoke_registry_create_polyfill_so(bespoke_registry_t* rr, renamer_t* renamer, uint16_t* ver_idx) {
  uint32_t polyfill_lib = sht_lookup_u(&rr->strings, "polyfill", 8);
  if (!polyfill_lib) return;
  for (uint32_t* itr = sht_iter_start_p(&rr->renames); itr; itr = sht_iter_next_p(&rr->renames, itr)) {
    if (itr[0] == polyfill_lib && itr[2] == 0) {
      const char* name = sht_u_key(&rr->strings, itr[1]);
      export_polyfill_symbol(renamer, name, ver_idx);
    }
  }
}

typedef struct target_ver_registry_t {
  const uint64_t* ver_table;
  uint32_t ver_mask;
  uint32_t target_ver_limit;
  const char* target_ver;
} target_ver_registry_t;

static void target_ver_registry_bind_to(target_ver_registry_t* rr, erw_state_t* erw) {
  const uint64_t* table;
  uint32_t mask;
  switch (erw->machine) {
  case EM_X86_64:
    table = (const uint64_t*)src_x86_64_renames_txt_data;
    mask = src_x86_64_renames_txt_mask;
    break;
  default:
    rr->target_ver_limit = 0;
    return;
  }
  rr->ver_table = table;
  rr->ver_mask = mask;

  uint32_t known_want_to_apply = 0;
  uint32_t target_ver_limit = ~(uint32_t)0;
  for (uint32_t i = 0; i <= mask; ++i) {
    uint64_t slot = table[i];
    if (slot) {
      uint32_t slot_val = (uint32_t)(slot >> 32);
      if (known_want_to_apply < slot_val && slot_val < target_ver_limit) {
        const uint32_t* payload = (const uint32_t*)((uint8_t*)table + slot_val);
        const char* vstr = (const char*)payload - payload[0] - 1;
        const char* under = strchr(vstr, '_');
        if (under) vstr = under + 1;
        if (version_str_less(rr->target_ver, vstr)) {
          known_want_to_apply = slot_val;
        } else {
          target_ver_limit = slot_val;
        }
      }
    }
  }
  rr->target_ver_limit = target_ver_limit;
}

static void target_ver_registry_create_polyfill_so(target_ver_registry_t* rr, renamer_t* renamer, uint16_t* ver_idx) {
  const uint64_t* table = rr->ver_table;
  uint32_t mask = rr->ver_mask;
  for (uint32_t idx = 0; idx <= mask; ++idx) {
    uint32_t slot_val = (uint32_t)(table[idx] >> 32);
    if (slot_val == 0 || slot_val >= rr->target_ver_limit) continue;
    const uint32_t* payload = (const uint32_t*)((uint8_t*)table + slot_val);
    uint32_t mask2 = payload[1];
    const uint64_t* table2 = (const uint64_t*)(payload + 2);
    for (uint32_t idx2 = 0; idx2 <= mask2; ++idx2) {
      uint32_t slot_val2 = (uint32_t)(table2[idx2] >> 32);
      if (slot_val2) {
        const uint8_t* payload2 = (const uint8_t*)table2 + slot_val2;
        uint8_t new_lib_len = payload2[1];
        uint8_t new_ver_len = payload2[3];
        if (new_lib_len == 0xff && new_ver_len == 0) {
          uint8_t new_name_len = payload2[0];
          const uint8_t* name = new_name_len ? payload2 + 4 : payload2 - payload2[2];
          export_polyfill_symbol(renamer, (const char*)name, ver_idx);
        }
      }
    }
  }
}

static bool target_ver_registry_apply_to_name(target_ver_registry_t* rr, renamed_symbol_t* sym) {
  if (!sym->version || rr->target_ver_limit == 0) {
    return false;
  }
  // Lookup the old version.
  uint32_t str_len = strlen(sym->version);
  uint32_t h = sht_hash_mem(sym->version, str_len);
  uint32_t d = 0;
  const uint64_t* table = rr->ver_table;
  uint32_t mask = rr->ver_mask;
  for (;;) {
    uint32_t idx = ((uint32_t)h + d) & mask;
    uint64_t slot = table[idx];
    if (!slot) {
      return false;
    }
    if ((uint32_t)slot == h) {
      uint32_t slot_val = (uint32_t)(slot >> 32);
      if (slot_val >= rr->target_ver_limit) {
        // Version found, but disabled by target_ver.
        return false;
      }
      const uint32_t* payload = (const uint32_t*)((uint8_t*)table + slot_val);
      if (payload[0] == str_len && memcmp(sym->version, ((const uint8_t*)payload) - str_len - 1, str_len) == 0) {
        mask = payload[1];
        table = (const uint64_t*)(payload + 2);
        break;
      }
    }
    uint32_t d2 = (idx - (uint32_t)slot) & mask;
    if (d2 < d) {
      return false;
    }
    ++d;
  }
  // Now lookup the old name.
  str_len = strlen(sym->name);
  if (str_len > 0xff) {
    return false;
  }
  h = sht_hash_mem(sym->name, str_len);
  d = 0;
  for (;;) {
    uint32_t idx = ((uint32_t)h + d) & mask;
    uint64_t slot = table[idx];
    if (!slot) {
      return false;
    }
    if ((uint32_t)slot == h) {
      const uint8_t* payload = (const uint8_t*)table + (slot >> 32);
      if (payload[2] == str_len && memcmp(sym->name, payload - str_len, str_len) == 0) {
        uint8_t new_lib_len = payload[1];
        uint8_t new_name_len = payload[0];
        uint8_t new_ver_len = payload[3];
        payload += 4;
        if (new_lib_len) {
          if (new_lib_len == 0xff) {
            sym->lib = "polyfill";
          } else {
            sym->lib = (const char*)payload;
            payload += new_lib_len;
          }
        }
        if (new_name_len) {
          sym->name = (const char*)payload;
          payload += new_name_len;
        }
        sym->version = new_ver_len ? (const char*)payload : NULL;
        return true;
      }
    }
    uint32_t d2 = (idx - (uint32_t)slot) & mask;
    if (d2 < d) {
      return false;
    }
    ++d;
  }
}

struct renamer_t {
  uintptr_t* registries;
  uint32_t registries_size;
  uint32_t registries_capacity;
  sht_t ensured_libs;
  renamer_options_t* options;
  erw_state_t* erw;
  const polyfiller_fns_t* polyfiller_fns;
  void* polyfiller;
  bool added_renames;
};

#define RR_BESPOKE_REGISTRY 1

static bool renamer_apply_to_name_one_step(renamer_t* self, renamed_symbol_t* sym) {
  uintptr_t* itr = self->registries;
  uintptr_t* end = itr + self->registries_size;
  while (itr < end) {
    uintptr_t rr = *itr++;
    if (rr & RR_BESPOKE_REGISTRY) {
      if (bespoke_registry_apply_to_name((bespoke_registry_t*)(rr - RR_BESPOKE_REGISTRY), sym)) {
        return true;
      }
    } else {
      if (target_ver_registry_apply_to_name((target_ver_registry_t*)rr, sym)) {
        return true;
      }
    }
  }
  return false;
}

bool renamer_apply_to_name(renamer_t* self, renamed_symbol_t* sym) {
  uint32_t num_renames = 0;
  while (renamer_apply_to_name_one_step(self, sym)) {
    if (++num_renames >= 1000) {
      FATAL("Infinite rename loop for %s%s%s", sym->name, sym->version ? "@" : "", sym->version ? sym->version : "");
    }
  }
  return num_renames != 0;
}

static uintptr_t renamer_add_registry(renamer_t* self, uintptr_t rr) {
  uintptr_t* registries = self->registries;
  uint32_t n = self->registries_size;
  if (n >= self->registries_capacity) {
    uint32_t new_capacity = n ? (n * 2) : 4;
    self->registries_capacity = new_capacity;
    self->registries = registries = realloc(registries, new_capacity * sizeof(*registries));
  }
  registries[n] = rr;
  self->registries_size = n + 1;
  return rr;
}

static uintptr_t renamer_add_bespoke_registry(renamer_t* self) {
  bespoke_registry_t* b = (bespoke_registry_t*)malloc(sizeof(bespoke_registry_t));
  sht_init(&b->strings, 0);
  sht_init(&b->renames, sizeof(uint32_t) * 3);
  return renamer_add_registry(self, (uintptr_t)b + RR_BESPOKE_REGISTRY);
}

static void bespoke_registry_free(bespoke_registry_t* rr) {
  sht_free(&rr->strings);
  sht_free(&rr->renames);
  free(rr);
}

void renamer_add_nodelete(renamer_t* self) {
  if (!self->options->create_polyfill_so) {
    erw_dhdrs_add_remove_flags(self->erw, DT_FLAGS_1, DF_1_NODELETE, 0);
  }
}

bool renamer_add_one_rename(renamer_t* self, const char* old_name, const char* new_name) {
  self->added_renames = true;
  uintptr_t rr;
  uint32_t registries_size = self->registries_size;
  if (!registries_size) {
    rr = renamer_add_bespoke_registry(self);
  } else {
    rr = *(self->registries + registries_size - 1);
    if (!(rr & RR_BESPOKE_REGISTRY)) {
      rr = renamer_add_bespoke_registry(self);
    }
  }
  return bespoke_registry_add_one((bespoke_registry_t*)(rr - RR_BESPOKE_REGISTRY), old_name, new_name);
}

void renamer_add_target_version_renames(renamer_t* self, const char* target_ver) {
  self->added_renames = true;
  target_ver_registry_t* t = (target_ver_registry_t*)malloc(sizeof(target_ver_registry_t));
  t->target_ver = target_ver;
  renamer_add_registry(self, (uintptr_t)t);
}

static void target_ver_registry_free(target_ver_registry_t* rr) {
  free(rr);
}

renamer_t* renamer_init(renamer_options_t* options) {
  size_t sz = sizeof(renamer_t) + (options ? 0 : sizeof(*options));
  renamer_t* rr = (renamer_t*)malloc(sz);
  memset(rr, 0, sz);
  rr->options = options ? options : (renamer_options_t*)(rr + 1);
  sht_init(&rr->ensured_libs, 1);
  return rr;
}

void renamer_free(renamer_t* self) {
  uintptr_t* itr = self->registries;
  uintptr_t* end = itr + self->registries_size;
  while (itr < end) {
    uintptr_t rr = *itr++;
    if (rr & RR_BESPOKE_REGISTRY) {
      bespoke_registry_free((bespoke_registry_t*)(rr - RR_BESPOKE_REGISTRY));
    } else {
      target_ver_registry_free((target_ver_registry_t*)rr);
    }
  }
  free(self->registries);
  sht_free(&self->ensured_libs);
  free(self);
}

void bind_target_ver_renamers_to(renamer_t* self, struct erw_state_t* erw) {
  uintptr_t* itr = self->registries;
  uintptr_t* end = itr + self->registries_size;
  while (itr < end) {
    uintptr_t rr = *itr++;
    if (!(rr & RR_BESPOKE_REGISTRY)) {
      target_ver_registry_bind_to((target_ver_registry_t*)rr, erw);
    }
  }
}

void renamer_ensure_lib_dependency(renamer_t* self, const char* lib) {
  if (lib) {
    *(unsigned char*)sht_intern_p(&self->ensured_libs, lib, strlen(lib)) = 0xff;
  }
}

static void ensure_has_polyfiller(renamer_t* self) {
  if (!self->polyfiller_fns) {
    self->polyfiller_fns = polyfiller_for_machine(self->erw->machine);
    self->polyfiller = (self->polyfiller_fns->init)(self->erw, self, self->options->polyfiller_cfi_mode);
  }
}

static void export_polyfill_symbol(renamer_t* renamer, const char* name, uint16_t* ver_idx) {
  ensure_has_polyfiller(renamer);
  uint8_t stt;
  uint32_t token = (renamer->polyfiller_fns->add)(renamer->polyfiller, name, &stt);
  if (!token) {
    FATAL("Missing implementation for polyfill::%s", name);
  }
  erw_state_t* erw = renamer->erw;
  uint32_t nsym = erw->dsyms.count;
  if (!*ver_idx) {
    *ver_idx = 0x8000 | erw_vers_find_or_add(erw, ver_idx, NULL, "POLYFILL");
  }
  uint32_t sidx = erw_dsyms_find_or_add(erw, name, *ver_idx, stt);
  if (sidx >= nsym) {
    if (erw->file_kind & ERW_FILE_KIND_64) {
      struct Elf64_Sym* syms = erw->dsyms.base;
      syms[sidx].st_value = (erw->file_kind & ERW_FILE_KIND_BSWAP) ? __builtin_bswap64(token) : token;
      syms[sidx].st_shndx = 0xffff;
    } else {
      struct Elf32_Sym* syms = erw->dsyms.base;
      syms[sidx].st_value = (erw->file_kind & ERW_FILE_KIND_BSWAP) ? __builtin_bswap32(token) : token;
      syms[sidx].st_shndx = 0xffff;
    }
    erw->dsyms.info[sidx].flags |= SYM_INFO_WANT_POLYFILL | SYM_INFO_EXPORTABLE | SYM_INFO_IN_HASH | SYM_INFO_IN_GNU_HASH;
  }
}

static void renamer_create_polyfill_so(renamer_t* self, uint16_t* ver) {
  uintptr_t* itr = self->registries;
  uintptr_t* end = itr + self->registries_size;
  while (itr < end) {
    uintptr_t rr = *itr++;
    if (rr & RR_BESPOKE_REGISTRY) {
      bespoke_registry_create_polyfill_so((bespoke_registry_t*)(rr - RR_BESPOKE_REGISTRY), self, ver);
    } else {
      target_ver_registry_create_polyfill_so((target_ver_registry_t*)rr, self, ver);
    }
  }
}

static bool xstreq(const char* lhs, const char* rhs) {
  if (lhs == NULL) {
    return rhs == NULL;
  } else {
    return rhs != NULL && strcmp(lhs, rhs) == 0;
  }
}

static void renamer_confirm_target_version(renamer_t* self);

#define NNE_INSTANTIATE "renamer_nne.h"
#include "nne_instantiator.h"

static void target_ver_registry_confirm(target_ver_registry_t* rr, renamer_t* renamer) {
  erw_state_t* erw = renamer->erw;
  if (!erw->dsyms.base) erw_dsyms_init(erw);

  const char* target_ver = rr->target_ver;
  if (version_str_less(target_ver, "2.5")) {
    // GLIBC_2.5 added support for DT_GNU_HASH. If targeting something older
    // than that, ensure we have a legacy hash table (or have neither table).
    if (erw->dsyms.want_gnu_hash) {
      erw_dsyms_add_hash(erw, DT_HASH);
    }
  }

  if (!erw->vers.count) erw_vers_init(erw);
  if (!erw->vers.need) return;
  bool* ver_bad = calloc(erw->vers.count, sizeof(bool));
  for (ver_group_t* g = erw->vers.need; g; g = g->next) {
    if (g->state & VER_EDIT_STATE_PENDING_DELETE) {
      continue;
    }
    for (ver_item_t* i = g->items; i; i = i->next) {
      if (i->state & VER_EDIT_STATE_PENDING_DELETE) {
        continue;
      }
      const char* vstr = erw_dynstr_decode(erw, i->str);
      if (has_prefix(vstr, "GLIBC_")) {
        vstr += 6;
        if (strcmp(vstr, "ABI_DT_RELR") == 0) {
          // ABI_DT_RELR was introduced with glibc 2.36, so we want to
          // try polyfilling it if targetting something before 2.36.
          vstr = "2.36";
          if (version_str_less(target_ver, vstr)) {
            if (erw_dhdrs_has(erw, DT_RELR, NULL)) {
              uint64_t dt_relrsz_value = 0;
              if (erw_dhdrs_has(erw, DT_RELRSZ, &dt_relrsz_value) && dt_relrsz_value) {
                if (renamer->options->use_polyfill_so) {
                  erw_relocs_expand_relr(erw);
                } else {
                  ensure_has_polyfiller(renamer);
                  if (!renamer->polyfiller_fns->add_fixup_for_dt_relr(renamer->polyfiller)) {
                    erw_relocs_expand_relr(erw);
                  }
                }
              }
            }
            erw_dhdrs_remove_mask(erw, (1ull << DT_RELR) | (1ull << DT_RELRSZ) | (1ull << DT_RELRENT));
          }
        }
      } else {
        continue;
      }
      if (version_str_less(target_ver, vstr)) {
        uint32_t index = i->index & 0x7fff;
        i->state |= VER_EDIT_STATE_REMOVED_REFS;
        if (erw->vers.base[index].item == i && !(i->flags & VER_FLG_WEAK)) {
          ver_bad[index] = true;
        }
      }
    }
  }

  u32_array_t bad_syms = {NULL, 0, 0};
  call_erwE_(collect_bad_version_syms, erw, ver_bad, &bad_syms);
  free(ver_bad);
  if (bad_syms.count) {
    fprintf(stderr, "Cannot change target version");
    if (erw->filename) {
      fprintf(stderr, " of %s", erw->filename);
    }
    fprintf(stderr, " to %s", target_ver);
    uint16_t machine = erw->machine;
    {
      const char* m_name = NULL;
      switch (machine) {
      case EM_386: m_name = "x86"; break;
      case EM_ARM: m_name = "arm"; break;
      case EM_X86_64: m_name = "x86_64"; break;
      case EM_AARCH64: m_name = "aarch64"; break;
      }
      if (m_name) {
        fprintf(stderr, " (%s)", m_name);
      } else {
        fprintf(stderr, " (machine type %u)", (unsigned)machine);
      }
    }
    fprintf(stderr, " due to missing knowledge about how to handle:\n");
    call_erwNNE_(print_bad_syms, erw, bad_syms.base, bad_syms.count);
    if (machine == EM_X86_64) {
      if (version_str_less(target_ver, "2.2.5")) {
        fprintf(stderr, "Note that 2.2.5 is the minimum version of glibc on x86_64; specifying a --target-glibc below this will always fail.\n");
      }
    } else {
      fprintf(stderr, "Note that --target-glibc is currently only supported for x86_64 files.\n");
    }
    fflush(stderr);
    exit(EXIT_FAILURE);
  }
}

static void renamer_confirm_target_version(renamer_t* self) {
  uintptr_t* itr = self->registries;
  uintptr_t* end = itr + self->registries_size;
  while (itr < end) {
    uintptr_t rr = *itr++;
    if (!(rr & RR_BESPOKE_REGISTRY)) {
      target_ver_registry_confirm((target_ver_registry_t*)rr, self);
    }
  }
}

void renamer_apply_to(renamer_t* self, erw_state_t* erw) {
  self->erw = erw;
  bind_target_ver_renamers_to(self, erw);
  if (!erw->dsyms.base) erw_dsyms_init(erw);
  if (!erw->vers.count) erw_vers_init(erw);
  call_erwNNE_(renamer_apply_to, erw, self);
  if (self->polyfiller_fns) {
    (self->polyfiller_fns->free)(self->polyfiller);
    self->polyfiller_fns = NULL;
    self->polyfiller = NULL;
  }
  self->erw = NULL;
}
