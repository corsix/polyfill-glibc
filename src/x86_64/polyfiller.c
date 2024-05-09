#include <stdint.h>
#include <string.h>
#include "../renamer.h"
#include "../common.h"
#include "../erw.h"
#include "../sht.h"
#include "../uuht.h"
#include "../polyfiller.h"
#include "assembler.h"
#include "../../build/x86_64/assembled_gen.h"
#define token_for(x) token_for_##x

typedef struct local_info_t {
  uint32_t token;
  uint32_t extra;
  uint64_t addr;
} local_info_t;

typedef struct polyfiller_t {
  erw_state_t* erw;
  renamer_t* renamer;
  local_info_t* addrs;
  uuht_t added;
  uint8_t cfi_mode;
  uint8_t seen_func_flags;
} polyfiller_t;

static void* polyfiller_init(erw_state_t* erw, renamer_t* renamer, enum polyfiller_cfi_mode cfi_mode) {
  polyfiller_t* result = (polyfiller_t*)malloc(sizeof(polyfiller_t));
  result->erw = erw;
  result->renamer = renamer;
  result->addrs = NULL;
  result->cfi_mode = cfi_mode;
  result->seen_func_flags = 0;
  uuht_init(&result->added);
  return result;
}

static void polyfiller_free(void* self_) {
  polyfiller_t* self = (polyfiller_t*)self_;
  uuht_free(&self->added);
  free(self->addrs);
  free(self);
}

static uint32_t rh_lookup(const char* name) {
  uint32_t name_len = strlen(name);
  uint32_t h = sht_hash_mem(name, name_len);
  uint32_t d = 0;
  uint64_t* table = (uint64_t*)polyfill_code;
  for (;;) {
    uint32_t idx = ((uint32_t)h + d) & polyfill_code_mask;
    uint64_t slot = table[idx];
    if (!slot) {
      return 0;
    }
    if ((uint32_t)slot == h) {
      const uint8_t* payload = polyfill_code + (slot >> 32);
      if (payload[1] == name_len && !memcmp(payload - 1 - name_len, name, name_len)) {
        return (uint32_t)(slot >> 32);
      }
    }
    uint32_t d2 = (idx - (uint32_t)slot) & polyfill_code_mask;
    if (d2 < d) {
      return 0;
    }
    ++d;
  }
}

static void parse_extern_sym(renamed_symbol_t* dst, const uint16_t* head) {
  uint32_t n_reloc = head[1];
  const uint32_t* relocs = (const uint32_t*)(head + 4);
  const uint8_t* src = (const uint8_t*)(relocs + n_reloc);
  uint8_t lib_len = src[0];
  uint8_t name_len = src[1];
  uint8_t ver_len = src[2];
  src += 3;
  dst->lib = lib_len ? (const char*)src : NULL;
  src += lib_len;
  dst->name = name_len ? (const char*)src : NULL;
  src += name_len;
  dst->version = ver_len ? (const char*)src : NULL;
}

#define SYM_WHY_REGULAR 3
#define SYM_WHY_GOT_SLOT 2

static void add_sym_and_deps(polyfiller_t* self, uint32_t token, uint32_t why) {
  uint32_t old_why = uuht_set_if_absent(&self->added, token, why);
  why |= old_why;
  if (why == old_why) return;
  if (old_why != 0) uuht_set(&self->added, token, why);

  const uint16_t* head = (const uint16_t*)(polyfill_code + token);
  uint32_t n_reloc = head[1];
  if (n_reloc) {
    const uint32_t* relocs = (const uint32_t*)(head + 4);
    const uint8_t* data = (const uint8_t*)(relocs + n_reloc);
    for (uint32_t i = 0; i < n_reloc; ++i) {
      uint32_t r = relocs[i];
      uint32_t val = r >> 8;
      uint32_t sub_why = SYM_WHY_REGULAR;
      if (!(r & RUN_RELOC_PHANTOM)) {
        const uint8_t* src = data + val;
        memcpy(&val, src, sizeof(val));
        if ((r & RUN_RELOC_CALL_OR_JMP) && src[-1] < 0xe8) {
          // If it happens to be an extern symbol that renames to a polyfill,
          // then a GOT slot is not required, as the call or jmp can be
          // rewritten to be relative.
          sub_why -= SYM_WHY_GOT_SLOT;
        }
      }
      add_sym_and_deps(self, val, sub_why);
    }
  }
  uint32_t flags = ((uint8_t*)head)[0];
  if (flags & ASM_SYM_FLAG_EXTERN) {
    renamed_symbol_t rsym;
    parse_extern_sym(&rsym, head);
    const char* name0 = rsym.name;
    if (name0 == NULL) {
      renamer_add_one_rename(self->renamer, rsym.lib, rsym.version);
    } else if (renamer_apply_to_name(self->renamer, &rsym)) {
      if (rsym.lib && strcmp(rsym.lib, "polyfill") == 0) {
        uint32_t val = rh_lookup(rsym.name);
        if (!val) {
          FATAL("Missing implementation for polyfill::%s (required by %s)", rsym.name, name0);
        }
        add_sym_and_deps(self, val, SYM_WHY_REGULAR);
      }
    }
  } else if (flags & ASM_SYM_FLAG_FUNCTION) {
    if (head[2] && head[3]) {
      flags |= ASM_SYM_FLAG_EXTERN; // Re-use to mean non-trivial CFI information
    }
    self->seen_func_flags |= flags;
  }
}

static uint32_t polyfiller_add(void* self, const char* name, uint8_t* stt) {
  uint32_t token = rh_lookup(name);
  if (token) {
    if (stt) {
      uint8_t flags = *(const uint8_t*)(polyfill_code + token);
      *stt = (flags & ASM_SYM_FLAG_FUNCTION) ? STT_FUNC : STT_OBJECT;
    }
    add_sym_and_deps((polyfiller_t*)self, token, SYM_WHY_REGULAR);
  }
  return token;
}

static bool polyfiller_add_fixup_for_dt_relr(void* self_) {
  polyfiller_t* self = (polyfiller_t*)self_;
  add_sym_and_deps(self, token_for(_polyfill_DT_RELR_apply), SYM_WHY_REGULAR);
  return true;
}

/*
Externs:
  Can rename to a polyfill, in which case jmp/call relocs can rewrite to rel32, but other reloc types need a GOT+reloc.
  Can have existing GOT+reloc+sym.
  Can have new GOT+reloc, existing sym.
  Can have new GOT+reloc+sym.
*/

static int sort_local_info_by_token(const void* lhs_, const void* rhs_) {
  const local_info_t* lhs = lhs_;
  const local_info_t* rhs = rhs_;
  uint32_t lhs_token = lhs->token;
  uint32_t rhs_token = rhs->token;
  if (lhs_token != rhs_token) {
    return lhs_token < rhs_token ? -1 : 1;
  }
  return 0;
}

static void populate_cfi_variables(polyfiller_t* self) {
#define polyfill_cie "\x14\0\0\0\0\0\0\0\01zR\0\01\x78\x10\01\x1b\x0c\07\x08\x90\01\0\0"
#define polyfill_fde_len 17
  uint32_t num_fde = 0;
  uint32_t size_fde = (sizeof(polyfill_cie) - 1) + 8;
  uint64_t in_trivial = 0;
  bool want_trivial = (self->cfi_mode != polyfiller_cfi_mode_minimal);
  for (local_info_t* sym = self->addrs, *end = sym + self->added.count; sym < end; ++sym) {
    const uint16_t* head = (const uint16_t*)(polyfill_code + sym->token);
    uint32_t flags = ((uint8_t*)head)[0];
    if (flags & ASM_SYM_FLAG_EXTERN) continue;
    if (!(flags & ASM_SYM_FLAG_FUNCTION)) continue;
    uint32_t n_code = head[2];
    if (!n_code) continue;
    uint32_t n_cfi = head[3];
    if (n_cfi) {
      in_trivial = 0;
    } else if (in_trivial) {
      continue;
    } else {
      in_trivial = 1;
      if (!want_trivial) continue;
    }
    num_fde += 1;
    size_fde += (polyfill_fde_len + n_cfi + 3) & ~(uint32_t)3;
  }
  local_info_t* var = self->addrs + uuht_lookup_or(&self->added, token_for(_polyfill_cfi_data), ~0u);
  uint64_t cfi_base_v = var->addr = erw_alloc(self->erw, erw_alloc_category_v_r, 4, size_fde);
  uint64_t hdr_v = 0;
  if (!erw_phdrs_find_first(self->erw, PT_GNU_EH_FRAME)) {
    uint32_t hdr_sz = sizeof(uint32_t) * (3 + 2 * num_fde);
    hdr_v = erw_alloc(self->erw, erw_alloc_category_v_r, 4, hdr_sz);
    erw_phdrs_add_gnu_eh_frame(self->erw, hdr_v, hdr_sz);
  }

  if (self->erw->retry) {
    return;
  }

  uint8_t* cfi_base = erw_view_v(self->erw, var->addr);
  int32_t* hdr = NULL;
  if (hdr_v) {
    hdr = (int32_t*)erw_view_v(self->erw, hdr_v);
    memcpy(hdr, "\01\x1b\03\x3b", 4);
    hdr[1] = cfi_base_v - (hdr_v + 4);
    hdr[2] = num_fde;
    hdr += 3;
  }
  memcpy(cfi_base, polyfill_cie, sizeof(polyfill_cie) - 1);
  size_fde = sizeof(polyfill_cie) - 1;
  in_trivial = 0;
  uint64_t prev_v = 0;
  for (local_info_t* sym = self->addrs, *end = sym + self->added.count; sym < end; ++sym) {
    const uint16_t* head = (const uint16_t*)(polyfill_code + sym->token);
    uint32_t flags = ((uint8_t*)head)[0];
    if (flags & ASM_SYM_FLAG_EXTERN) continue;
    if (!(flags & ASM_SYM_FLAG_FUNCTION)) continue;
    uint32_t n_code = head[2];
    if (!n_code) continue;
    if (sym->addr < prev_v) FATAL("symbols not in ascending address order");
    prev_v = sym->addr;
    uint32_t n_cfi = head[3];
    if (n_cfi) {
      in_trivial = 0;
    } else if (!want_trivial) {
      continue;
    } else if (in_trivial) {
      uint32_t* this_fde = (uint32_t*)(cfi_base + size_fde - ((polyfill_fde_len + 3) & ~(uint32_t)3));
      this_fde[3] = sym->addr + n_code - in_trivial; // Increase size to cover this sym.
      continue;
    } else {
      in_trivial = sym->addr;
    }
    if (hdr) {
      *hdr++ = sym->addr - hdr_v; // Offset to function start.
      *hdr++ = (cfi_base_v + size_fde) - hdr_v; // Offset to FDE.
    }
    uint32_t size_this_fde = (polyfill_fde_len + n_cfi + 3) & ~(uint32_t)3;
    uint32_t* this_fde = (uint32_t*)(cfi_base + size_fde);
    this_fde[0] = size_this_fde - 4; // Size of FDE.
    this_fde[1] = size_fde + 4; // Offset to CIE.
    this_fde[2] = sym->addr - (cfi_base_v + size_fde + 8); // Offset to function start.
    this_fde[3] = n_code; // Size of function.
    uint8_t* this_fde_tail = (uint8_t*)(this_fde + 4);
    this_fde_tail[0] = 0; // 'z' augmentation data
    uint32_t n_reloc = head[1];
    const uint32_t* relocs = (const uint32_t*)(head + 4);
    const uint8_t* code = (const uint8_t*)(relocs + n_reloc);
    memcpy(this_fde_tail + 1, code + n_code, n_cfi);
    size_fde += size_this_fde;
  }
#undef polyfill_cie
#undef polyfill_fde_len
}

#define VARIABLE_ALREADY_POPULATED 1

static void populate_init_variables(polyfiller_t* self, uint32_t num_init_fn) {
  local_info_t* var = self->addrs + uuht_lookup_or(&self->added, token_for(_polyfill_init_array), ~0u);
  var->addr = erw_alloc(self->erw, erw_alloc_category_v_r, 4, (num_init_fn + 1) * 4);
  if (!self->erw->retry) {
    uint32_t* dst = (uint32_t*)erw_view_v(self->erw, var->addr);
    dst[0] = num_init_fn;
    num_init_fn = 0;
    for (local_info_t* sym = self->addrs, *end = sym + self->added.count; sym < end; ++sym) {
      const uint16_t* head = (const uint16_t*)(polyfill_code + sym->token);
      uint32_t flags = ((uint8_t*)head)[0];
      if (!(flags & ASM_SYM_FLAG_INIT)) continue;
      if (!(flags & ASM_SYM_FLAG_FUNCTION)) continue;
      dst[++num_init_fn] = sym->addr - var->addr;
    }
  }
  uint64_t dt_init_val;
  if (erw_dhdrs_has(self->erw, DT_INIT, &dt_init_val)) {
    if (!self->erw->retry) {
      var = self->addrs + uuht_lookup_or(&self->added, token_for(_polyfill_DT_INIT_original), ~0u);
      dt_init_val -= var->addr;
      var->extra = VARIABLE_ALREADY_POPULATED;
      memcpy(erw_view_v(self->erw, var->addr), &dt_init_val, sizeof(dt_init_val));
      erw_dhdrs_set_u(self->erw, DT_INIT, self->addrs[uuht_lookup_or(&self->added, token_for(_polyfill_DT_INIT), ~0u)].addr);
    }
  } else {
    erw_dhdrs_set_u(self->erw, DT_INIT, self->addrs[uuht_lookup_or(&self->added, token_for(_polyfill_init), ~0u)].addr);
  }
  if (!self->erw->retry && erw_phdrs_is_entry_callable(self->erw)) {
    uint64_t old_entry = erw_set_entry(self->erw, self->addrs[uuht_lookup_or(&self->added, token_for(_polyfill_entry), ~0u)].addr);
    var = self->addrs + uuht_lookup_or(&self->added, token_for(_polyfill_entry_original), ~0u);
    old_entry -= var->addr;
    var->extra = VARIABLE_ALREADY_POPULATED;
    memcpy(erw_view_v(self->erw, var->addr), &old_entry, sizeof(old_entry));
  }
}

static void populate_fini_variables(polyfiller_t* self, uint32_t num_fini_fn) {
  local_info_t* var = self->addrs + uuht_lookup_or(&self->added, token_for(_polyfill_fini_array), ~0u);
  uint64_t dt_fini_value;
  bool has_fini = erw_dhdrs_has(self->erw, DT_FINI, &dt_fini_value);
  num_fini_fn += has_fini;
  if (num_fini_fn <= 1) {
    // At most one DT_FINI function; things are simple.
    var->addr = erw_alloc(self->erw, erw_alloc_category_v_r, 8, 8);
    if (num_fini_fn == 1 && !has_fini) {
      if (self->erw->retry) {
        erw_dhdrs_set_u(self->erw, DT_FINI, 0);
      } else {
        for (local_info_t* sym = self->addrs, *end = sym + self->added.count; sym < end; ++sym) {
          const uint16_t* head = (const uint16_t*)(polyfill_code + sym->token);
          uint32_t flags = ((uint8_t*)head)[0];
          if (!(flags & ASM_SYM_FLAG_FINI)) continue;
          if (!(flags & ASM_SYM_FLAG_FUNCTION)) continue;
          erw_dhdrs_set_u(self->erw, DT_FINI, sym->addr);
        }
      }
    }
    return;
  }
  // Need to construct our own fini array.
  var->addr = erw_alloc(self->erw, erw_alloc_category_v_r, 8, (num_fini_fn + 1) * 8);
  if (!self->erw->retry) {
    uint64_t* dst = (uint64_t*)erw_view_v(self->erw, var->addr);
    dst[0] = num_fini_fn;
    num_fini_fn = 0;
    if (has_fini) {
      // The old DT_FINI, if any, is called first.
      dst[++num_fini_fn] = dt_fini_value - var->addr;
    }
    for (local_info_t* sym = self->addrs, *end = sym + self->added.count; sym < end; ++sym) {
      const uint16_t* head = (const uint16_t*)(polyfill_code + sym->token);
      uint32_t flags = ((uint8_t*)head)[0];
      if (!(flags & ASM_SYM_FLAG_FINI)) continue;
      if (!(flags & ASM_SYM_FLAG_FUNCTION)) continue;
      dst[++num_fini_fn] = sym->addr - var->addr;
    }
  }
  erw_dhdrs_set_u(self->erw, DT_FINI, self->addrs[uuht_lookup_or(&self->added, token_for(_polyfill_DT_FINI), ~0u)].addr);
}

static void populate_dt_relr_variables(polyfiller_t* self) {
  local_info_t* fn = self->addrs + uuht_lookup_or(&self->added, token_for(_polyfill_DT_RELR_apply), ~0u);
  uint64_t dummy_v = erw_alloc(self->erw, erw_alloc_category_v_rw_zero, 8, 8);
  erw_relocs_add_early(self->erw, DT_RELA, R_X86_64_IRELATIVE, dummy_v, 0, fn->addr);

  uint64_t dt_relr_value = 0, dt_relrsz_value = 0;
  (void)erw_dhdrs_has(self->erw, DT_RELR, &dt_relr_value);
  (void)erw_dhdrs_has(self->erw, DT_RELRSZ, &dt_relrsz_value);
  erw_dhdrs_remove_mask(self->erw, (1ull << DT_RELR) | (1ull << DT_RELRSZ) | (1ull << DT_RELRENT));
  if (!self->erw->retry) {
    local_info_t* var = self->addrs + uuht_lookup_or(&self->added, token_for(_polyfill_DT_RELR_data), ~0u);
    uint64_t var_addr = var->addr;
    uint64_t* dst = (uint64_t*)erw_view_v(self->erw, var_addr);
    dst[0] = var_addr;
    dst[1] = dt_relrsz_value;
    dst[2] = dt_relr_value - var_addr;
    var->extra = VARIABLE_ALREADY_POPULATED;
  }
}

static bool new_got_slot_for_extern(polyfiller_t* self, local_info_t* sym) {
  const uint16_t* head = (const uint16_t*)(polyfill_code + sym->token);
  uint32_t flags = ((uint8_t*)head)[0];
  if (!(flags & ASM_SYM_FLAG_EXTERN)) return false;
  renamed_symbol_t rsym;
  parse_extern_sym(&rsym, head);
  if (rsym.name == NULL) return false;
  if (renamer_apply_to_name(self->renamer, &rsym) && rsym.lib && strcmp(rsym.lib, "polyfill") == 0) return false;
  uint16_t ver_cache = 0;
  uint16_t vidx = erw_vers_find_or_add(self->erw, &ver_cache, rsym.lib, rsym.version);
  uint32_t sidx = erw_dsyms_find_or_add(self->erw, rsym.name, vidx, (flags & ASM_SYM_FLAG_FUNCTION) ? STT_FUNC : STT_OBJECT);
  self->erw->dsyms.info[sidx].flags |= SYM_INFO_RELOC_EAGER | SYM_INFO_RELOC_REGULAR;
  sym->addr = erw_alloc(self->erw, erw_alloc_category_v_rw_zero, 8, 8);
  erw_relocs_add(self->erw, DT_RELA, R_X86_64_GLOB_DAT, sym->addr, sidx, 0);
  return true;
}

static void populate_regular_symbol(polyfiller_t* self, local_info_t* sym, const uint16_t* head) {
  uint32_t n_reloc = head[1];
  uint32_t n_code = head[2];
  const uint32_t* relocs = (const uint32_t*)(head + 4);
  const uint8_t* src = (const uint8_t*)(relocs + n_reloc);
  uint8_t* dst = erw_view_v(self->erw, sym->addr);
  if (!dst) {
    // This happens for variables placed into erw_alloc_category_v_rw_zero.
    return;
  }
  memcpy(dst, src, n_code);
  for (uint32_t i = 0; i < n_reloc; ++i) {
    uint32_t r = relocs[i];
    if (r & RUN_RELOC_PHANTOM) continue;
    uint32_t ofs = r >> 8;
    uint32_t val;
    memcpy(&val, src + ofs, sizeof(val));
    val = uuht_lookup_or(&self->added, val, ~0u);
    local_info_t* target = self->addrs + val;
    if ((r & RUN_RELOC_CALL_OR_JMP) && *(dst + ofs - 1) < 0xe8 && target->extra) {
      // This is jmp / call to an extern symbol that was renamed to a polyfill.
      // Rewrite ff /2 -> rex e8, ff /4 -> rex e9.
      val = target->extra;
      val = uuht_lookup_or(&self->added, val, ~0u);
      target = self->addrs + val;
      *(dst + ofs - 2) = 0x40;
      *(dst + ofs - 1) = 0xe8 + ((*(dst + ofs - 1) - 0x15) >> 4);
    }
    uint64_t src_v = sym->addr + ofs + 4;
    if (r & RUN_RELOC_IMM_SIZE_MASK) {
      src_v += 1u << ((r & RUN_RELOC_IMM_SIZE_MASK) - 1);
    }
    if (target->addr == 0) {
      FATAL("Relocation target not given an address");
    }
    uint64_t delta = target->addr - src_v;
    if ((int32_t)(uint32_t)delta != (int64_t)delta) {
      // This can happen for extern symbols if they have an pre-existing
      // relocation in the file, but said relocation is too far away from
      // our new code. Try to fix it by giving the extern a new address.
      if (new_got_slot_for_extern(self, target)) {
        delta = target->addr - src_v;
      }
      if ((int32_t)(uint32_t)delta != (int64_t)delta) {
        FATAL("Cannot encode relocation from %llx to %llx in 32 bits", (long long unsigned)src_v, (long long unsigned)target->addr);
      }
    }
    val = (uint32_t)delta;
    memcpy(dst + ofs, &val, sizeof(val));
    if ((r & RUN_RELOC_CALL_OR_JMP) && *(dst + ofs - 1) == 0xe9) {
      // This relocation is to a jmp instruction; optimisations are sometimes possible.
      delta += 3;
      if ((int8_t)(uint8_t)delta == (int64_t)delta) {
        // Rewrite jmp rel32 to jmp rel8; int3; int3; int3.
        *(dst + ofs - 1) = 0xeb;
        dst[ofs] = (uint8_t)delta;
        *(dst + ofs + 1) = 0xcc;
        *(dst + ofs + 2) = 0xcc;
        *(dst + ofs + 3) = 0xcc;
        if ((uint8_t)delta <= 11u && ofs + 4 == n_code) {
          // Short jumps at the end of a function, which by definition must
          // be going to the next function, can use a nop rather than a jmp.
          delta += 2; // Length of the jmp rel8.
          const char* nops = "\x90" "\x66\x90" "\x0f\x1f\x00" "\x0f\x1f\x40\x00" "\x0f\x1f\x44\x00\x00" "\x66\x0f\x1f\x44\x00\x00" "\x0f\x1f\x80\x00\x00\x00\x00" "\x0f\x1f\x84\x00\x00\x00\x00\x00" "\x66\x0f\x1f\x84\x00\x00\x00\x00\x00";
          if (delta > 9) {
            // More than 9 bytes requires two nops. The first nop can be at
            // most 4 bytes long, as all instruction boundaries need to be
            // within the function bounds as specified in CFI data, and we're
            // replacing a 5 byte jmp.
            memcpy(dst + ofs - 1, nops + 6, 4);
            delta -= 4;
            ofs += 4;
          }
          memcpy(dst + ofs - 1, nops + (delta * (delta - 1)) / 2, delta);
        }
      }
    }
  }
}

static int sort_u64_asc(const void* lhs, const void* rhs) {
  uint64_t a = *(const uint64_t*)lhs;
  uint64_t b = *(const uint64_t*)rhs;
  if (a != b) {
    return a < b ? -1 : 1;
  }
  return 0;
}

#define NNE_INSTANTIATE "x86_64/polyfiller_nne.h"
#include "../nne_instantiator.h"

static void polyfiller_finished_adding(void* self_) {
  polyfiller_t* self = (polyfiller_t*)self_;
  // Add any internal symbols that we need.
  bool any_cfi = false;
  if (self->seen_func_flags) {
    switch ((enum polyfiller_cfi_mode)self->cfi_mode) {
    case polyfiller_cfi_mode_none:
      break;
    case polyfiller_cfi_mode_auto:
      if (!erw_phdrs_find_first(self->erw, PT_GNU_EH_FRAME)) {
        any_cfi = true;
      }
      // fallthrough
    case polyfiller_cfi_mode_minimal:
      if (self->seen_func_flags & ASM_SYM_FLAG_EXTERN) {
        // Seen at least one function with non-trivial CFI information?
        any_cfi = true;
      }
      break;
    case polyfiller_cfi_mode_full:
      any_cfi = true;
      break;
    }
    if (any_cfi) {
      // Need to register the CFI data somehow.
      if (!erw_phdrs_find_first(self->erw, PT_GNU_EH_FRAME)) {
        // No PT_GNU_EH_FRAME present, so we can add one and point it at our CFI data.
        add_sym_and_deps(self, token_for(_polyfill_cfi_data), SYM_WHY_REGULAR);
      } else if (erw_dhdrs_has_needed(self->erw, "libgcc_s.so.1")) {
        // There is already a libgcc_s.so.1 dependency, so we can use the cheap code
        // that directly calls __(de)register_frame_info.
        add_sym_and_deps(self, token_for(_polyfill_init_cfi_strong), SYM_WHY_REGULAR);
      } else {
        // Need to use the more complex code that tries to dlopen libgcc_s.so.1, and if
        // present, tries to dlsym __(de)register_frame_info, and if present, calls them.
        add_sym_and_deps(self, token_for(_polyfill_init_cfi_weak), SYM_WHY_REGULAR);
      }
    }
    // Note that self->seen_func_flags might have gained ASM_SYM_FLAG_INIT due to CFI stuff.
    if (self->seen_func_flags & ASM_SYM_FLAG_INIT) {
      // Need to ensure init functions get called.
      if (erw_dhdrs_has(self->erw, DT_INIT, NULL)) {
        // Wrapper around existing DT_INIT.
        add_sym_and_deps(self, token_for(_polyfill_DT_INIT), SYM_WHY_REGULAR);
      } else {
        // Added as a new DT_INIT.
        add_sym_and_deps(self, token_for(_polyfill_init), SYM_WHY_REGULAR);
      }
      if (erw_phdrs_is_entry_callable(self->erw)) {
        // Wrapper around entry point.
        add_sym_and_deps(self, token_for(_polyfill_entry), SYM_WHY_REGULAR);
      }
    }
    // Note that self->seen_func_flags might have gained ASM_SYM_FLAG_FINI due to init stuff.
    if (self->seen_func_flags & ASM_SYM_FLAG_FINI) {
      // Need to ensure fini functions get called.
      add_sym_and_deps(self, token_for(_polyfill_DT_FINI), SYM_WHY_REGULAR);
    }
  }

  // Really have finished adding symbols now; turn the set into a sorted list.
  self->addrs = calloc(self->added.count, sizeof(*self->addrs));
  for (uint32_t i = 0, j = 0; i <= self->added.mask; ++i) {
    uint64_t slot = self->added.table[i];
    if (slot) {
      local_info_t* sym = self->addrs + j++;
      sym->token = uuht_unhash((uint32_t)slot);
      sym->addr = slot; // Putting this here temporarily; will become addr in the next pass.
      sym->extra = i;   // Putting this here temporarily; will become extra in the next pass.
    }
  }
  qsort(self->addrs, self->added.count, sizeof(self->addrs[0]), sort_local_info_by_token);

  // Assign addresses to all symbols.
  uint16_t ver_cache = 0;
  uint32_t num_init_fn = 0;
  uint32_t num_fini_fn = 0;
  uuht_t dsym_to_ext;
  uuht_init(&dsym_to_ext);
  for (local_info_t* sym = self->addrs, *end = sym + self->added.count; sym < end; ++sym) {
    const uint16_t* head = (const uint16_t*)(polyfill_code + sym->token);
    uint32_t flags = ((uint8_t*)head)[0];
    uint32_t n_code = head[2];
    uint64_t old_slot = sym->addr;
    self->added.table[sym->extra] = (uint32_t)old_slot | (((uint64_t)(sym - self->addrs)) << 32);
    uint64_t v = 0;
    uint32_t extra = 0;
    if (flags & ASM_SYM_FLAG_EXTERN) {
      renamed_symbol_t rsym;
      parse_extern_sym(&rsym, head);
      if (rsym.name == NULL) {
        // Imposed rename rather than extern symbol.
      } else if (renamer_apply_to_name(self->renamer, &rsym) && rsym.lib && strcmp(rsym.lib, "polyfill") == 0) {
        extra = rh_lookup(rsym.name);
        if ((old_slot >> 32) & SYM_WHY_GOT_SLOT) {
          // Renamed to polyfill, but a GOT slot is still required.
          // Said GOT slot is populated using a relative relocation.
          if (extra > sym->token) {
            // Should not happen, as the assembler ensures all extern symbols
            // come last, but if it were to happen, it would be a problem, as
            // we need the address of the renamed symbol at this point in order
            // to install the relocation.
            FATAL("Extern symbol renamed to a polyfill that appears after it");
          }
          v = erw_alloc(self->erw, erw_alloc_category_v_rw_zero, 8, 8);
          local_info_t* rinfo = self->addrs + uuht_lookup_or(&self->added, extra, ~0u);
          erw_relocs_add(self->erw, DT_RELA, R_X86_64_RELATIVE, v, 0, rinfo->addr);
        }
      } else {
        renamer_ensure_lib_dependency(self->renamer, rsym.lib);
        uint16_t vidx = erw_vers_find_or_add(self->erw, &ver_cache, rsym.lib, rsym.version);
        uint32_t sidx = erw_dsyms_find_or_add(self->erw, rsym.name, vidx, (flags & ASM_SYM_FLAG_FUNCTION) ? STT_FUNC : STT_OBJECT);
        uint32_t own_idx = (uint32_t)(sym - self->addrs);
        own_idx ^= uuht_lookup_or_set(&dsym_to_ext, sidx, own_idx);
        self->added.table[sym->extra] ^= (uint64_t)own_idx << 32;
      }
    } else if (flags & ASM_SYM_FLAG_FUNCTION) {
      num_init_fn += !!(flags & ASM_SYM_FLAG_INIT);
      num_fini_fn += !!(flags & ASM_SYM_FLAG_FINI);
      v = erw_alloc(self->erw, erw_alloc_category_v_rx, 16, n_code);
    } else {
      v = erw_alloc(self->erw, (enum erw_alloc_category)(head[3] & 0xff), head[3] >> 8, n_code);
    }
    sym->addr = v;
    sym->extra = extra;
  }
  if (dsym_to_ext.count) {
    if (!self->erw->relocs[0].base) erw_relocs_init(self->erw);
    call_erwNNE_(find_existing_relocs, self->erw, &dsym_to_ext, self->addrs);
    // Any externs that didn't have an existing reloc need to have a reloc created now.
    uint32_t n_ext = 0;
    for (uint32_t i = 0; i <= dsym_to_ext.mask; ++i) {
      uint64_t slot = dsym_to_ext.table[i];
      if (slot) {
        slot = ((uint64_t)uuht_unhash(slot) << 32) | (slot >> 32);
        dsym_to_ext.table[n_ext++] = slot;
      }
    }
    qsort(dsym_to_ext.table, n_ext, sizeof(dsym_to_ext.table[0]), sort_u64_asc);
    for (uint32_t i = 0; i < n_ext; ++i) {
      uint64_t slot = dsym_to_ext.table[i];
      local_info_t* sym = self->addrs + (uint32_t)slot;
      if (sym->addr) continue;
      uint32_t sidx = slot >> 32;
      self->erw->dsyms.info[sidx].flags |= SYM_INFO_RELOC_EAGER | SYM_INFO_RELOC_REGULAR;
      sym->addr = erw_alloc(self->erw, erw_alloc_category_v_rw_zero, 8, 8);
      erw_relocs_add(self->erw, DT_RELA, R_X86_64_GLOB_DAT, sym->addr, sidx, 0);
    }
  }
  uuht_free(&dsym_to_ext);

  // Populate internal symbols (this can allocate virtual memory and/or change
  // the address of an internal symbol).
  if (num_init_fn) {
    populate_init_variables(self, num_init_fn);
  }
  if (num_fini_fn) {
    populate_fini_variables(self, num_fini_fn);
  }
  if (any_cfi) {
    populate_cfi_variables(self);
  }
  if (uuht_contains(&self->added, token_for(_polyfill_DT_RELR_apply))) {
    populate_dt_relr_variables(self);
  }

  if (self->erw->retry) {
    return;
  }

  // Populate non-internal symbols.
  for (local_info_t* sym = self->addrs, *end = sym + self->added.count; sym < end; ++sym) {
    const uint16_t* head = (const uint16_t*)(polyfill_code + sym->token);
    uint32_t flags = ((uint8_t*)head)[0];
    if (flags & ASM_SYM_FLAG_EXTERN) continue;
    if (sym->extra) continue;
    populate_regular_symbol(self, sym, head);
  }
}

static uint64_t polyfiller_addr_of(void* self_, uint32_t token) {
  polyfiller_t* self = (polyfiller_t*)self_;
  return self->addrs[uuht_lookup_or(&self->added, token, ~(uint32_t)0)].addr;
}

const polyfiller_fns_t polyfiller_fns_x86_64 = {
  .init = polyfiller_init,
  .add_fixup_for_dt_relr = polyfiller_add_fixup_for_dt_relr,
  .add = polyfiller_add,
  .finished_adding = polyfiller_finished_adding,
  .addr_of = polyfiller_addr_of,
  .free = polyfiller_free
};
