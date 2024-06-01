#include "assembler.h"
#include "../common.h"
#include "../elf.h"
#include "../uuht.h"
#include "../../build/x86_64/relative_interp_trampoline.h"
#include <string.h>

typedef struct {
  char* result;
  char* elf_file;
  struct Elf64_Shdr* shdr;
  uint32_t* order;
  uuht_t sect_to_rela;
  uuht_t gathered;
} gathered_sects_t;

static uint32_t layout_sections(gathered_sects_t* gs, uint32_t off) {
  uint32_t n = gs->gathered.count;
  for (uint32_t pass = 0; pass < 2; ++pass) {
    for (uint32_t i = 0; i < n; ++i) {
      uint32_t idx = gs->order[i];
      struct Elf64_Shdr* shdr = gs->shdr + idx;
      uintptr_t flags = shdr->sh_flags;
      if ((uint32_t)!(flags & SHF_EXECINSTR) != pass) {
        // All SHF_EXECINSTR sections in pass 0.
        // Then all !SHF_EXECINSTR sections in pass 1.
        continue;
      }
      ASSERT(!(flags & SHF_WRITE));
      ASSERT(flags & SHF_ALLOC);
      off = (off + shdr->sh_addralign - 1) &- shdr->sh_addralign;
      uuht_set(&gs->gathered, idx, off);
      off += shdr->sh_size;
    }
  }
  return off;
}

static void need_sym(gathered_sects_t* gs, struct Elf64_Sym* syms, uint32_t idx) {
  // Convert symbol index to section index.
  if (!idx) return;
  idx = syms[idx].st_shndx;
  ASSERT(idx != SHN_UNDEF && idx != SHN_ABS);

  // If seeing it for the first time, add section index to our ordered list.
  if (uuht_set(&gs->gathered, idx, 1)) return;
  (gs->order + gs->gathered.count)[-1] = idx;

  // Also need any symbols referenced by relocations.
  idx = uuht_lookup_or(&gs->sect_to_rela, idx, 0);
  if (!idx) return;
  struct Elf64_Shdr* shdr = gs->shdr + idx;
  struct Elf64_Rela* rela = (struct Elf64_Rela*)(gs->elf_file + shdr->sh_offset);
  struct Elf64_Rela* end = (struct Elf64_Rela*)((char*)rela + shdr->sh_size);
  syms = (struct Elf64_Sym*)(gs->elf_file + gs->shdr[shdr->sh_link].sh_offset);
  for (; rela < end; ++rela) {
    need_sym(gs, syms, rela->r_info >> 32);
  }
}

static uint32_t sym_addr(gathered_sects_t* gs, struct Elf64_Sym* sym) {
  uint32_t section = sym->st_shndx;
  return uuht_lookup_or(&gs->gathered, section, 0) + sym->st_value;
}

static void render_asm(gathered_sects_t* gs, uint32_t token, uint32_t root_sym, uint32_t result_size) {
  // Copy the contents.
  const uint16_t* head = (const uint16_t*)(polyfill_code + token);
  uint32_t n_reloc = head[1];
  uint32_t n_code = head[2];
  const uint32_t* relocs = (const uint32_t*)(head + 4);
  memcpy(gs->result, relocs + n_reloc, n_code);

  // Then apply relocations.
  for (uint32_t i = 0; i < n_reloc; ++i) {
    uint32_t r = relocs[i];
    if (r & RUN_RELOC_PHANTOM) continue;
    uint32_t ofs = r >> 8;
    uint32_t val;
    memcpy(&val, gs->result + ofs, sizeof(val));
    uint32_t target;
    switch (val) {
    case token_for__polyfill_c_function: target = root_sym; break;
    case token_for__polyfill_our_data: target = result_size; break;
    default: FATAL("Unexpected relocation target %d", (int)val);
    }
    uint32_t src_v = ofs + 4 + ((1u << (r & RUN_RELOC_IMM_SIZE_MASK)) >> 1);
    val = target - src_v;
    memcpy(gs->result + ofs, &val, sizeof(val));
  }
}

static void render_section(gathered_sects_t* gs, uint32_t idx) {
  // Copy the contents.
  uint32_t off = uuht_lookup_or(&gs->gathered, idx, 0);
  struct Elf64_Shdr* shdr = gs->shdr + idx;
  memcpy(gs->result + off, gs->elf_file + shdr->sh_offset, shdr->sh_size);

  // Then apply relocations.
  idx = uuht_lookup_or(&gs->sect_to_rela, idx, 0);
  if (!idx) return;
  shdr = gs->shdr + idx;
  struct Elf64_Rela* rela = (struct Elf64_Rela*)(gs->elf_file + shdr->sh_offset);
  struct Elf64_Rela* end = (struct Elf64_Rela*)((char*)rela + shdr->sh_size);
  struct Elf64_Sym* syms = (struct Elf64_Sym*)(gs->elf_file + gs->shdr[shdr->sh_link].sh_offset);
  for (; rela < end; ++rela) {
    uint32_t kind = (uint32_t)rela->r_info;
    uint32_t target = sym_addr(gs, syms + (rela->r_info >> 32)) + rela->r_addend;
    uint32_t ofs = off + rela->r_offset;
    switch (kind) {
    case R_X86_64_PC32:
      *(uint32_t*)(gs->result + ofs) = target - ofs;
      break;
    default:
      FATAL("Unsupported reloc kind %d", (int)kind);
    }
  }
}

static void emit_bytes(uint8_t* src, uint32_t n, FILE* f) {
  if (n) {
    uint32_t i = 0;
    uint8_t b;
    for (;;) {
      b = src[i];
      if (++i >= n) break;
      fprintf(f, "0x%02x, ", b);
      if (!(i & 127)) fprintf(f, "\n");
    }
    fprintf(f, "0x%02x\n", b);
  }
}

int main(int argc, const char** argv) {
  if (argc < 3) {
    FATAL("Usage: %s INPUT.o OUTPUT.h\n", argv[0]);
  }

  gathered_sects_t gs;
  gs.elf_file = read_entire_file(argv[1], NULL);
  struct Elf64_Ehdr* ehdr = (struct Elf64_Ehdr*)gs.elf_file;
  gs.shdr = (struct Elf64_Shdr*)(gs.elf_file + ehdr->e_shoff);
  uint32_t shnum = ehdr->e_shnum;
  uuht_init(&gs.sect_to_rela);
  uuht_init(&gs.gathered);
  gs.order = calloc(shnum, sizeof(uint32_t));

  struct Elf64_Shdr* shdr_symtab = NULL;
  for (uint32_t i = 0; i < shnum; ++i) {
    struct Elf64_Shdr* itr = gs.shdr + i;
    switch (itr->sh_type) {
    case SHT_SYMTAB:
      ASSERT(!shdr_symtab);
      shdr_symtab = itr;
      break;
    case SHT_RELA:
      ASSERT(uuht_set(&gs.sect_to_rela, itr->sh_info, i) == 0);
      break;
    }
  }
  ASSERT(shdr_symtab);

  // Find root symbol.
  struct Elf64_Sym* sym = (struct Elf64_Sym*)(gs.elf_file + shdr_symtab->sh_offset);
  uint32_t symnum = shdr_symtab->sh_size / shdr_symtab->sh_entsize;
  uint32_t root = 0;
  for (uint32_t i = 0; i < symnum; ++i) {
    struct Elf64_Sym* itr = sym + i;
    if (itr->st_shndx != SHN_UNDEF && itr->st_info == ((STB_GLOBAL << 4) | STT_FUNC)) {
      ASSERT(!root);
      root = i;
    }
  }
  ASSERT(root);
  need_sym(&gs, sym, root);

  uint32_t token = token_for__polyfill_trampoline;
  uint32_t size = *(uint16_t*)(polyfill_code + token + 4); // asm code first
  size = layout_sections(&gs, size); // then all the sections
  size = (size + 8 - 1) & -8; // then padding to 8 byte boundary

  gs.result = malloc(size);
  memset(gs.result, 0xCC, size);
  render_asm(&gs, token, sym_addr(&gs, sym + root), size);
  for (uint32_t i = 0; i < gs.gathered.count; ++i) {
    render_section(&gs, gs.order[i]);
  }

  const char* out_path = argv[2];
  FILE* f = fopen(out_path, "w");
  if (!f) FATAL("Could not open %s for writing", out_path);
  fprintf(f, "static const uint8_t relative_interp_payload[] = {\n");
  emit_bytes((uint8_t*)gs.result, size, f);
  fprintf(f, "};\n");
  fclose(f);

  free(gs.result);
  free(gs.order);
  uuht_free(&gs.gathered);
  uuht_free(&gs.sect_to_rela);
  free(gs.elf_file);
  return 0;
}
