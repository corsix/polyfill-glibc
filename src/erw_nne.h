
static void erwNNE_(v2f_init)(erw_state_t* erw) {
  struct ElfNN_(Phdr)* itr = (struct ElfNN_(Phdr)*)erw->phdrs.base;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  uint64_t page_size = erw->guest_page_size;
  for (; itr != end; ++itr) {
    if (Elf_bswapu32(itr->p_type) != PT_LOAD) continue;
    uint64_t pgoff = Elf_bswapuNN(itr->p_vaddr) & (page_size - 1);
    uint64_t fbase = Elf_bswapuNN(itr->p_offset) - pgoff;
    uint64_t vbase = Elf_bswapuNN(itr->p_vaddr) - pgoff;
    // Map whole pages from file
    uint64_t fend = (Elf_bswapuNN(itr->p_offset) + Elf_bswapuNN(itr->p_filesz) + page_size - 1) &- page_size;
    if (fend <= erw->f_size) {
      v2f_map_set_range(&erw->v2f, vbase, fend - fbase, fbase - vbase);
    } else if (fbase <= erw->f_size) {
      v2f_map_set_range(&erw->v2f, vbase, erw->f_size - fbase, fbase - vbase);
      v2f_map_set_range(&erw->v2f, vbase + (erw->f_size - fbase), fend - erw->f_size, V2F_ZEROFILL);
    } else {
      v2f_map_set_range(&erw->v2f, vbase, fend - fbase, V2F_ZEROFILL);
    }
    // Zero-fill filesz through memsz
    if (Elf_bswapuNN(itr->p_memsz) > Elf_bswapuNN(itr->p_filesz)) {
      v2f_map_set_range(&erw->v2f, Elf_bswapuNN(itr->p_vaddr) + Elf_bswapuNN(itr->p_filesz), Elf_bswapuNN(itr->p_memsz) - Elf_bswapuNN(itr->p_filesz), V2F_ZEROFILL);
    }
  }
}

static struct ElfNN_(Shdr)* erwNNE_(shdr_find)(erw_state_t* erw, const char* name) {
  struct ElfNN_(Ehdr)* e = (struct ElfNN_(Ehdr)*)erw->f;
  if (Elf_bswapu16(e->e_shentsize) != sizeof(struct ElfNN_(Shdr))) return NULL;
  if (Elf_bswapuNN(e->e_shoff) > erw->f_size) return NULL;
  uint64_t num = (erw->f_size - Elf_bswapuNN(e->e_shoff)) / sizeof(struct ElfNN_(Shdr));
  if (num > Elf_bswapu16(e->e_shnum)) num = Elf_bswapu16(e->e_shnum);
  uint32_t shstrndx = Elf_bswapu16(e->e_shstrndx);
  if (!shstrndx || shstrndx >= num) return NULL;
  struct ElfNN_(Shdr)* itr = (struct ElfNN_(Shdr)*)(erw->f + Elf_bswapuNN(e->e_shoff));
  struct ElfNN_(Shdr)* end = itr + num;
  Elf_uNN s_base = Elf_bswapuNN(itr[shstrndx].sh_offset);
  do {
    Elf_uNN s_off = s_base + Elf_bswapu32(itr->sh_name);
    if (s_off < erw->f_size && !strcmp(name, (const char*)(erw->f + s_off))) {
      erw->modified |= ERW_MODIFIED_SHDRS;
      return itr;
    }
  } while (++itr < end);
  return NULL;
}

static struct ElfNN_(Shdr)* erwNNE_(shdr_update_from_phdr)(erw_state_t* erw, const char* name, uint32_t type, struct ElfNN_(Phdr)* itr) {
  struct ElfNN_(Shdr)* shdr = erwNNE_(shdr_find)(erw, name);
  if (shdr) {
    shdr->sh_type = Elf_bswapu32(type);
    Elf_uNN flags = Elf_bswapuNN(shdr->sh_flags) & ~(Elf_uNN)(SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR);
    if ((shdr->sh_addr = itr->p_vaddr)) flags |= SHF_ALLOC;
    if (itr->p_flags & Elf_bswapu32(PF_W)) flags |= SHF_WRITE;
    if (itr->p_flags & Elf_bswapu32(PF_X)) flags |= SHF_EXECINSTR;
    shdr->sh_flags = Elf_bswapuNN(flags);
    shdr->sh_offset = itr->p_offset;
    shdr->sh_size = itr->p_filesz;
    shdr->sh_addralign = itr->p_align;
  }
  return shdr;
}

static void erwNNE_(shdr_fixup_section_symbols)(erw_state_t* erw) {
  struct ElfNN_(Ehdr)* e = (struct ElfNN_(Ehdr)*)erw->f;
  if (Elf_bswapu16(e->e_shentsize) != sizeof(struct ElfNN_(Shdr))) return;
  if (Elf_bswapuNN(e->e_shoff) > erw->f_size) return;
  uint64_t num_shdr = (erw->f_size - Elf_bswapuNN(e->e_shoff)) / sizeof(struct ElfNN_(Shdr));
  if (num_shdr > Elf_bswapu16(e->e_shnum)) num_shdr = Elf_bswapu16(e->e_shnum);

  struct ElfNN_(Shdr)* shdrs = (struct ElfNN_(Shdr)*)(erw->f + Elf_bswapuNN(e->e_shoff));
  struct ElfNN_(Shdr)* itr = shdrs;
  struct ElfNN_(Shdr)* end = itr + num_shdr;
  for (; itr < end; ++itr) {
    uint32_t type = Elf_bswapu32(itr->sh_type);
    if ((type == SHT_DYNSYM || type == SHT_SYMTAB) && Elf_bswapuNN(itr->sh_entsize) == sizeof(struct ElfNN_(Sym))) {
      Elf_uNN off = Elf_bswapuNN(itr->sh_offset);
      Elf_uNN sz = Elf_bswapuNN(itr->sh_size);
      if (off < erw->f_size) {
        if ((erw->f_size - off) < sz) {
          sz = erw->f_size - off;
        }
        struct ElfNN_(Sym)* sym = (struct ElfNN_(Sym)*)(erw->f + off);
        struct ElfNN_(Sym)* sym_end = sym + (sz / sizeof(struct ElfNN_(Sym)));
        for (; sym < sym_end; ++sym) {
          if ((sym->st_info & 0xf) == STT_SECTION) {
            uint32_t shndx = Elf_bswapu16(sym->st_shndx);
            if (shndx < num_shdr) {
              sym->st_value = shdrs[shndx].sh_addr;
            }
          }
        }
      }
    }
  }
}

static uint64_t erwNNE_(set_entry)(erw_state_t* erw, uint64_t new_entry) {
  struct ElfNN_(Ehdr)* h = (struct ElfNN_(Ehdr)*)erw->f;
  uint64_t result = Elf_bswapuNN(h->e_entry);
  if (result != new_entry) {
    h->e_entry = Elf_bswapuNN(new_entry);
    erw->modified |= ERW_MODIFIED_MISC;
  }
  return result;
}

static void erwNNE_(phdrs_init)(erw_state_t* erw) {
  struct ElfNN_(Ehdr)* hdr = (struct ElfNN_(Ehdr)*)erw->f;
  erw->machine = Elf_bswapu16(hdr->e_machine);
  if (erw->guest_page_size == 0) {
    switch (erw->machine) {
    case EM_TILEGX:
    case EM_TILEPRO:
      erw->guest_page_size = 0x10000;
      break;
    case EM_IA_64:
      erw->guest_page_size = 0x4000;
      break;
    case EM_ALPHA:
    case EM_SPARC:
    case EM_SPARCV9:
      erw->guest_page_size = 0x2000;
      break;
    default:
      erw->guest_page_size = 0x1000;
      break;
    }
  }
  Elf_uNN phoff = Elf_bswapuNN(hdr->e_phoff);
  erw->phdrs.base = (struct ElfNN_(Phdr)*)(erw->f + phoff);
  erw->phdrs.count = phoff < erw->f_size ? ((erw->f_size - phoff) / sizeof(struct ElfNN_(Phdr))) : 0;
  if (Elf_bswapu16(hdr->e_phnum) < erw->phdrs.count) {
    Elf_uNN shoff = Elf_bswapuNN(hdr->e_shoff);
    if (hdr->e_phnum == 0xffff && shoff < erw->f_size && (erw->f_size - shoff) >= sizeof(struct ElfNN_(Shdr))) {
      uint32_t count = Elf_bswapu32(((struct ElfNN_(Shdr)*)(erw->f + shoff))->sh_info);
      if (count < erw->phdrs.count) erw->phdrs.count = count;
    } else {
      erw->phdrs.count = Elf_bswapu16(hdr->e_phnum);
    }
  }
  erw->phdrs.capacity = 0;
  erw_original_state_t* orig = (erw_original_state_t*)malloc(sizeof(erw_original_state_t) + sizeof(struct ElfNN_(Phdr)) * erw->phdrs.count);
  erw->original = orig;
  orig->f_size = erw->f_size;
  orig->phdr_count = erw->phdrs.count;
  memcpy(orig + 1, erw->phdrs.base, sizeof(struct ElfNN_(Phdr)) * erw->phdrs.count);
  v2f_map_init(&erw->v2f, V2F_UNMAPPED);
  erwNNE_(v2f_init)(erw);
}

static struct ElfNN_(Phdr)* erwNNE_(phdrs_alloc_early)(erw_state_t* erw) {
  struct ElfNN_(Phdr)* itr = erw->phdrs.base;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  struct ElfNN_(Phdr)* ld = NULL;
  for (; itr != end; ++itr) {
    uint32_t type = Elf_bswapu32(itr->p_type);
    if (type == PT_NULL) {
      return itr;
    }
    if (type == PT_LOAD) {
      ld = itr++;
      break;
    }
  }
  for (; itr != end; ++itr) {
    if (itr->p_type == PT_NULL) {
      memmove(ld + 1, ld, (char*)itr - (char*)ld);
      ld->p_type = PT_NULL;
      return ld;
    }
  }
  if (erw->phdrs.count >= erw->phdrs.capacity) {
    erw_phdrs_increase_capacity(erw);
  }
  itr = erw->phdrs.base;
  memmove(itr + 1, itr, erw->phdrs.count++ * sizeof(*itr));
  return itr;
}

#ifdef ERW_NATIVE_ENDIAN
static struct ElfNN_(Phdr)* erwNN_(phdrs_alloc)(erw_state_t* erw) {
  struct ElfNN_(Phdr)* itr = erw->phdrs.base;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  for (; itr != end; ++itr) {
    if (itr->p_type == PT_NULL) {
      return itr;
    }
  }
  if (erw->phdrs.count >= erw->phdrs.capacity) {
    erw_phdrs_increase_capacity(erw);
  }
  itr = erw->phdrs.base;
  return itr + erw->phdrs.count++;
}
#endif

static void erwNNE_(phdrs_add_gnu_eh_frame)(erw_state_t* erw, uint64_t v, uint64_t sz) {
  struct ElfNN_(Phdr)* itr = erwNN_(phdrs_alloc)(erw);
  itr->p_type = Elf_bswapu32(PT_GNU_EH_FRAME);
  itr->p_offset = Elf_bswapuNN(v + v2f_map_lookup(&erw->v2f, v)->v2f);
  itr->p_filesz = Elf_bswapuNN(sz);
  itr->p_vaddr = itr->p_paddr = Elf_bswapuNN(v);
  itr->p_memsz = Elf_bswapuNN(sz);
  itr->p_flags = Elf_bswapu32(PF_R);
  itr->p_align = Elf_bswapuNN(4);
  erw->modified |= ERW_MODIFIED_PHDRS;
}

static void erwNNE_(phdrs_set_interpreter)(erw_state_t* erw, const char* value) {
  struct ElfNN_(Phdr)* itr = erw->phdrs.base;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  size_t alen = strlen(value) + 1;
  for (; itr != end; ++itr) {
    if (Elf_bswapu32(itr->p_type) == PT_INTERP) {
      Elf_uNN off = Elf_bswapuNN(itr->p_offset);
      if (Elf_bswapuNN(itr->p_filesz) >= alen && off < erw->f_size && (erw->f_size - off) > alen && memcmp(erw->f + off, value, alen) == 0) {
        return;
      }
      goto found_phdr;
    }
  }
  itr = erwNNE_(phdrs_alloc_early)(erw);
  itr->p_type = Elf_bswapu32(PT_INTERP);
found_phdr:
  erw->modified |= ERW_MODIFIED_MISC | ERW_MODIFIED_PHDRS;
  Elf_uNN new_off = erw_alloc(erw, erw_alloc_category_f, 1, alen);
  if (!new_off || erw->retry) return;
  memcpy(erw->f + new_off, value, alen);
  itr->p_offset = Elf_bswapuNN(new_off);
  itr->p_filesz = Elf_bswapuNN(alen);
  v2f_entry_t* e = v2f_map_lookup_f_range(&erw->v2f, new_off, alen);
  if (e) {
    itr->p_vaddr = itr->p_paddr = Elf_bswapuNN(new_off - e->v2f);
    itr->p_memsz = Elf_bswapuNN(alen);
  } else {
    itr->p_vaddr = itr->p_paddr = 0;
    itr->p_memsz = 0;
  }
  itr->p_flags = Elf_bswapu32(PF_R);
  itr->p_align = Elf_bswapuNN(1);
  erwNNE_(shdr_update_from_phdr)(erw, ".interp", SHT_PROGBITS, itr);
}

static void erwNNE_(phdrs_set_stack_prot)(erw_state_t* erw, uint32_t prot) {
  struct ElfNN_(Phdr)* itr = erw->phdrs.base;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  bool any = false;
  for (; itr != end; ++itr) {
    if (Elf_bswapu32(itr->p_type) == PT_GNU_STACK) {
      uint32_t flags0 = Elf_bswapu32(itr->p_flags);
      uint32_t flags = (flags0 & ~(uint32_t)(PF_R | PF_W | PF_X)) | prot;
      if (flags != flags0) {
        itr->p_flags = Elf_bswapu32(flags);
        erw->modified |= ERW_MODIFIED_PHDRS;
        any = true;
      }
    }
  }
  if (!any && prot != (PF_R | PF_W | PF_X)) {
    erw->modified |= ERW_MODIFIED_PHDRS;
    itr = erwNN_(phdrs_alloc)(erw);
    memset(itr, 0, sizeof(*itr));
    itr->p_type = Elf_bswapu32(PT_GNU_STACK);
    itr->p_flags = Elf_bswapu32(prot);
    itr->p_align = Elf_bswapuNN(16);
  }
}

#ifdef ERW_NATIVE_ENDIAN
static void erwNN_(phdrs_remove)(erw_state_t* erw, uint32_t tag) {
  struct ElfNN_(Phdr)* itr = erw->phdrs.base;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  if (erw->file_kind & ERW_FILE_KIND_BSWAP) {
    tag = __builtin_bswap32(tag);
  }
  for (; itr != end; ++itr) {
    if (itr->p_type == tag) {
      erw->modified |= ERW_MODIFIED_PHDRS;
      memset(itr, 0, sizeof(*itr));
    }
  }
}
#endif

#ifdef ERW_NATIVE_ENDIAN
static void* erwNN_(phdrs_find_first)(erw_state_t* erw, uint32_t tag) {
  struct ElfNN_(Phdr)* itr = erw->phdrs.base;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  if (erw->file_kind & ERW_FILE_KIND_BSWAP) tag = __builtin_bswap32(tag);
  for (; itr != end; ++itr) {
    if (itr->p_type == tag) {
      return itr;
    }
  }
  return NULL;
}
#endif

static bool erwNNE_(phdrs_is_entry_callable)(erw_state_t* erw) {
  Elf_uNN entry = Elf_bswapuNN(((struct ElfNN_(Ehdr)*)erw->f)->e_entry);
  Elf_uNN page_size = erw->guest_page_size;
  struct ElfNN_(Phdr)* itr = erw->phdrs.base;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  for (; itr != end; ++itr) {
    if (Elf_bswapu32(itr->p_type) == PT_LOAD && (Elf_bswapu32(itr->p_flags) & PF_X)) {
      Elf_uNN vlo = Elf_bswapuNN(itr->p_vaddr) &- page_size;
      if (vlo <= entry) {
        Elf_uNN vhi = (Elf_bswapuNN(itr->p_vaddr) + Elf_bswapuNN(itr->p_memsz) + page_size - 1) &- page_size;
        if (entry < vhi) {
          return true;
        }
      }
    }
  }
  return false;
}

static bool erwNNE_(phdrs_bug_workaround)(erw_state_t* erw, Elf_uNN required_v2f) {
  if (erw->phdrs.count == erw->phdrs.capacity) {
    erw_phdrs_increase_capacity(erw);
  }
  struct ElfNN_(Phdr)* itr = erw->phdrs.base;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  for (; itr != end; ++itr) {
    if (Elf_bswapu32(itr->p_type) == PT_LOAD) {
      if (Elf_bswapuNN(itr->p_offset) == Elf_bswapuNN(itr->p_vaddr) + required_v2f) {
        // First PT_LOAD already has the required v2f.
        return false;
      } else if (itr->p_filesz == 0) {
        // Can re-purpose existing zero-size entry.
        itr->p_offset = Elf_bswapuNN(Elf_bswapuNN(itr->p_vaddr) + required_v2f);
        return false;
      } else {
        // Insert a dummy zero-size entry with the required v2f.
        memmove(itr + 1, itr, (char*)end - (char*)itr);
        itr->p_flags = Elf_bswapu32(PF_R | PF_W);
        itr->p_offset = Elf_bswapuNN(Elf_bswapuNN(itr->p_vaddr) + required_v2f);
        itr->p_filesz = 0;
        itr->p_memsz = 0;
        ++erw->phdrs.count;
        return true;
      }
    }
  }
  // No PT_LOAD present; the one we add will be first, and have the desired v2f.
  return false; 
}

static void erwNNE_(phdrs_flush)(erw_state_t* erw) {
  size_t nb = erw->phdrs.count * sizeof(struct ElfNN_(Phdr));
  if (erw->phdrs.count <= erw->original->phdr_count && !(erw->alloc_buckets.category_mask & ~(1u << erw_alloc_category_f))) {
    // If no additional virtual memory needs to be allocated, and new phdrs can
    // fit in where the original phdrs were, then overwrite the originals.
    struct ElfNN_(Ehdr)* h = (struct ElfNN_(Ehdr)*)erw->f;
    struct ElfNN_(Phdr)* dst = (struct ElfNN_(Phdr)*)(erw->f + Elf_bswapu64(h->e_phoff));
    memcpy(dst, erw->phdrs.base, nb);
    memset((char*)dst + nb, 0, (erw->original->phdr_count - erw->phdrs.count) * sizeof(struct ElfNN_(Phdr)));
    free(erw->phdrs.base);
    erw->phdrs.base = dst;
    erw->phdrs.capacity = 0;
    return;
  }
  uint64_t v = erw_alloc(erw, erw_alloc_category_v_r, sizeof(Elf_uNN), nb);
  if (!v) {
    if (erw->f_size == erw->original->f_size) {
      // First time through? Allocate what we think we'll need for erw_retry.
      // This hopefully reduces the number of iterations required to converge.
      erw_alloc(erw, erw_alloc_category_v_r, sizeof(Elf_uNN), extra_phdrs_required_for_category_mask(erw->alloc_buckets.category_mask) * sizeof(struct ElfNN_(Phdr)));
    }
    return;
  }
  uint64_t v2f = v2f_map_lookup(&erw->v2f, v)->v2f;
  uint64_t f = v + v2f;
  if (erwNNE_(phdrs_bug_workaround)(erw, v2f)) {
    // We added one additional phdr as a bug workaround, account for it now.
    nb += sizeof(struct ElfNN_(Phdr));
    erw_alloc(erw, erw_alloc_category_v_r, sizeof(Elf_uNN), sizeof(struct ElfNN_(Phdr)));
  }
  if (!(erw->modified & ERW_MODIFIED_DYNSTR) && erw_dhdrs_has(erw, DT_SONAME, NULL)) {
    // Due to a bug in ldconfig versions prior to 2.31, if DT_SONAME is present, then
    // DT_STRTAB needs its v2f to be the v2f of the first PT_LOAD, which is also what
    // we're forcing the v2f of the phdrs to be.
    uint64_t strtab;
    if (erw_dhdrs_has(erw, DT_STRTAB, &strtab) && v2f_map_lookup(&erw->v2f, strtab)->v2f != v2f) {
      erw->modified |= ERW_MODIFIED_DYNSTR;
      erw->retry = true;
    }
  }
  if (erw->retry) {
    // Going around again? No need to actually flush the changes.
    return;
  }
  // Write out the new phdrs.
  struct ElfNN_(Phdr)* dst = (struct ElfNN_(Phdr)*)(erw->f + f);
  memcpy(dst, erw->phdrs.base, nb);
  free(erw->phdrs.base);
  erw->phdrs.base = dst;
  erw->phdrs.capacity = 0;
  // Update ELF header to point to new phdrs.
  struct ElfNN_(Ehdr)* h = (struct ElfNN_(Ehdr)*)erw->f;
  h->e_phoff = Elf_bswapuNN(f);
  if (erw->phdrs.count < 0xffff) {
    h->e_phnum = Elf_bswapu16(erw->phdrs.count);
  } else {
    h->e_phnum = 0xffff;
    ((struct ElfNN_(Shdr)*)(erw->f + Elf_bswapuNN(h->e_shoff)))->sh_info = Elf_bswapu32(erw->phdrs.count);
  }
  // Update any PT_PHDR headers to point to new phdrs.
  struct ElfNN_(Phdr)* itr = dst;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  for (; itr != end; ++itr) {
    if (Elf_bswapu32(itr->p_type) == PT_PHDR) {
      itr->p_offset = Elf_bswapuNN(f);
      itr->p_vaddr = itr->p_paddr = Elf_bswapuNN(v);
      itr->p_filesz = itr->p_memsz = Elf_bswapuNN(nb);
      itr->p_flags = Elf_bswapu32(PF_R);
      itr->p_align = Elf_bswapuNN(sizeof(Elf_uNN));
    }
  }
}

void erwNNE_(dhdrs_init)(erw_state_t* erw) {
  struct ElfNN_(Phdr)* found = NULL;
  struct ElfNN_(Phdr)* itr = erw->phdrs.base;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  for (; itr != end; ++itr) {
    if (Elf_bswapu32(itr->p_type) == PT_DYNAMIC) {
      found = itr;
    }
  }
  if (found && found->p_filesz && Elf_bswapuNN(found->p_memsz) >= sizeof(struct ElfNN_(Dyn))) {
    uint32_t count = Elf_bswapuNN(found->p_memsz) / sizeof(struct ElfNN_(Dyn));
    uint64_t addr = Elf_bswapuNN(found->p_vaddr);
    v2f_entry_t* e = v2f_map_lookup(&erw->v2f, addr);
    if (!(e->v2f & V2F_SPECIAL) && (e[1].vbase - addr) >= count * sizeof(struct ElfNN_(Dyn))) {
      erw->dhdrs.base = (void*)(erw->f + (addr + e->v2f));
      erw->dhdrs.capacity = 0;
    } else {
      erw->dhdrs.base = malloc(count * sizeof(struct ElfNN_(Dyn)));
      erw->dhdrs.capacity = count;
      materialise_v_range_to(erw, addr, count * sizeof(struct ElfNN_(Dyn)), (char*)erw->dhdrs.base);
    }
    // Ignore any entries after the first (span of) DT_NULL
    struct ElfNN_(Dyn)* base = erw->dhdrs.base;
    uint32_t i = 0;
    while (i < count && base[i].d_tag != DT_NULL) ++i;
    while (i < count && base[i].d_tag == DT_NULL) ++i;
    erw->dhdrs.count = i;
  } else {
    erw->dhdrs.base = (void*)erw->f;
    erw->dhdrs.count = 0;
    erw->dhdrs.capacity = 0;
  }
}

#ifdef ERW_NATIVE_ENDIAN
static void erwNN_(dhdrs_remove)(erw_state_t* erw, Elf_uNN tag) {
  struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
  struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
  if (erw->file_kind & ERW_FILE_KIND_BSWAP) {
#ifdef ERW_32
    tag = __builtin_bswap32(tag);
#else
    tag = __builtin_bswap64(tag);
#endif
  }
  for (; itr != end; ++itr) {
    if (itr->d_tag == tag) {
      erw->modified |= ERW_MODIFIED_DHDRS;
      memset(itr, 0, sizeof(*itr));
    }
  }
}
#endif

static void erwNNE_(dhdrs_remove_mask)(erw_state_t* erw, uint64_t mask) {
  struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
  struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
  for (; itr != end; ++itr) {
    uint64_t tag = Elf_bswapuNN(itr->d_tag);
    if (tag <= 63 && (mask & (1ull << tag))) {
      erw->modified |= ERW_MODIFIED_DHDRS;
      memset(itr, 0, sizeof(*itr));
    }
  }
}

static Elf_uNN erwNNE_(dhdrs_find_value)(erw_state_t* erw, Elf_uNN tag) {
  struct ElfNN_(Dyn)* base = erw->dhdrs.base;
  struct ElfNN_(Dyn)* itr = base + erw->dhdrs.count;
  tag = Elf_bswapuNN(tag);
  while (base < itr) {
    if ((--itr)->d_tag == tag) {
      return Elf_bswapuNN(itr->d_un.d_val);
    }
  }
  return 0;
}

static void erwNNE_(dhdrs_set_u)(erw_state_t* erw, Elf_uNN tag, Elf_uNN value) {
  struct ElfNN_(Dyn)* base = erw->dhdrs.base;
  struct ElfNN_(Dyn)* itr = base + erw->dhdrs.count;
  struct ElfNN_(Dyn)* nul = NULL;
  bool any = false;
  bool modified = false;
  tag = Elf_bswapuNN(tag);
  value = Elf_bswapuNN(value);
  while (base < itr--) {
    if (itr->d_tag == tag) {
      any = true;
      if (itr->d_un.d_val != value) {
        itr->d_un.d_val = value;
        modified = true;
      }
    } else if (itr->d_tag == DT_NULL) {
      nul = itr;
    }
  }
  if (!any) {
    modified = true;
    if (!nul) {
      if (erw->dhdrs.count >= erw->dhdrs.capacity) {
        erw_dhdrs_increase_capacity(erw);
      }
      nul = (struct ElfNN_(Dyn)*)erw->dhdrs.base + erw->dhdrs.count++;
    }
    nul->d_tag = tag;
    nul->d_un.d_val = value;
  }
  if (modified) {
    erw->modified |= ERW_MODIFIED_DHDRS;
  }
}

static uint64_t erwNNE_(dhdrs_get_flags)(erw_state_t* erw) {
  uint32_t dt_flags = 0;
  uint32_t dt_1_flags = 0;
  uint32_t legacy_flags = 0;
  struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
  struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
  for (; itr != end; ++itr) {
    switch (Elf_bswapuNN(itr->d_tag)) {
    case DT_FLAGS:    dt_flags = Elf_bswapuNN(itr->d_un.d_val); break;
    case DT_FLAGS_1:  dt_1_flags = Elf_bswapuNN(itr->d_un.d_val); break;
    case DT_SYMBOLIC: legacy_flags |= DF_SYMBOLIC; break;
    case DT_TEXTREL:  legacy_flags |= DF_TEXTREL; break;
    case DT_BIND_NOW: legacy_flags |= DF_BIND_NOW; break;
    }
  }
  return (dt_flags | legacy_flags) | ((uint64_t)dt_1_flags << 32);
}

static void erwNNE_(dhdrs_add_remove_flags)(erw_state_t* erw, Elf_uNN tag, uint32_t add, uint32_t rem) {
  struct ElfNN_(Dyn)* base = erw->dhdrs.base;
  struct ElfNN_(Dyn)* itr = base + erw->dhdrs.count;
  struct ElfNN_(Dyn)* nul = NULL;
  tag = Elf_bswapuNN(tag);
  while (base < itr--) {
    if (itr->d_tag == tag) {
      Elf_uNN val0 = Elf_bswapuNN(itr->d_un.d_val);
      Elf_uNN val = val0;
      val |= add;
      val &= ~(Elf_uNN)rem;
      if (val != val0) {
        itr->d_un.d_val = Elf_bswapuNN(val);
        erw->modified |= ERW_MODIFIED_DHDRS;
        if (!val) {
          /* All flags unset, can replace with DT_NULL. */
          itr->d_tag = DT_NULL;
          while (base < itr--) {
            /* But also need to null out any earlier occurrences of tag. */
            if (itr->d_tag == tag) {
              memset(itr, 0, sizeof(*itr));
            }
          }
        }
      }
      return;
    } else if (itr->d_tag == DT_NULL) {
      nul = itr;
    }
  }
  if (add) {
    erw->modified |= ERW_MODIFIED_DHDRS;
    if (!nul) {
      if (erw->dhdrs.count >= erw->dhdrs.capacity) {
        erw_dhdrs_increase_capacity(erw);
      }
      nul = (struct ElfNN_(Dyn)*)erw->dhdrs.base + erw->dhdrs.count++;
    }
    nul->d_tag = tag;
    nul->d_un.d_val = Elf_bswapuNN(add);
  }
}

static void erwNNE_(dhdrs_set_str)(erw_state_t* erw, Elf_uNN tag, const char* str) {
  struct ElfNN_(Dyn)* base = erw->dhdrs.base;
  struct ElfNN_(Dyn)* itr = base + erw->dhdrs.count;
  struct ElfNN_(Dyn)* nul = NULL;
  bool any = false;
  bool modified = false;
  tag = Elf_bswapuNN(tag);
  while (base < itr--) {
    if (itr->d_tag == tag) {
      any = true;
      if (!strcmp(str, erw_dynstr_decode(erw, Elf_bswapuNN(itr->d_un.d_val)))) {
        itr->d_un.d_val = Elf_bswapuNN(erw_dynstr_encode(erw, str));
        modified = true;
      }
    } else if (itr->d_tag == DT_NULL) {
      nul = itr;
    }
  }
  if (!any) {
    uint64_t encoded = erw_dynstr_encode(erw, str);
    modified = true;
    if (!nul) {
      if (erw->dhdrs.count >= erw->dhdrs.capacity) {
        erw_dhdrs_increase_capacity(erw);
      }
      nul = (struct ElfNN_(Dyn)*)erw->dhdrs.base + erw->dhdrs.count++;
    }
    nul->d_tag = tag;
    nul->d_un.d_val = Elf_bswapuNN(encoded);
  }
  if (modified) {
    erw->modified |= ERW_MODIFIED_DHDRS;
  }
}

static bool erwNNE_(dhdrs_has)(erw_state_t* erw, Elf_uNN tag, uint64_t* value) {
  struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
  struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
  tag = Elf_bswapuNN(tag);
  for (; itr != end; ++itr) {
    if (itr->d_tag == tag) {
      if (value) *value = Elf_bswapuNN(itr->d_un.d_val);
      return true;
    }
  }
  return false;
}

static bool erwNNE_(dhdrs_has_needed)(erw_state_t* erw, const char* str) {
  struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
  struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
  for (; itr != end; ++itr) {
    if (Elf_bswapuNN(itr->d_tag) == DT_NEEDED) {
      if (strcmp(erw_dynstr_decode(erw, Elf_bswapuNN(itr->d_un.d_val)), str) == 0) {
        return true;
      }
    }
  }
  return false;
}

static void erwNNE_(dhdrs_add_early_u)(erw_state_t* erw, Elf_uNN tag, Elf_uNN value) {
  if (erw->dhdrs.count >= erw->dhdrs.capacity) {
    erw_dhdrs_increase_capacity(erw);
  }
  struct ElfNN_(Dyn)* itr = (struct ElfNN_(Dyn)*)erw->dhdrs.base;
  memmove(itr + 1, itr, sizeof(*itr) * erw->dhdrs.count++);
  itr->d_tag = Elf_bswapuNN(tag);
  itr->d_un.d_val = Elf_bswapuNN(value);
  erw->modified |= ERW_MODIFIED_DHDRS;
}

static void erwNNE_(dhdrs_add_late_u)(erw_state_t* erw, Elf_uNN tag, Elf_uNN value) {
  if (erw->dhdrs.count >= erw->dhdrs.capacity) {
    erw_dhdrs_increase_capacity(erw);
  }
  struct ElfNN_(Dyn)* itr = (struct ElfNN_(Dyn)*)erw->dhdrs.base + erw->dhdrs.count++;
  itr->d_tag = Elf_bswapuNN(tag);
  itr->d_un.d_val = Elf_bswapuNN(value);
  erw->modified |= ERW_MODIFIED_DHDRS;
}

static void erwNNE_(dhdrs_remove_needed)(erw_state_t* erw, const char* str) {
  struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
  struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
  bool found = false;
  for (; itr != end; ++itr) {
    if (Elf_bswapuNN(itr->d_tag) == DT_NEEDED) {
      if (strcmp(erw_dynstr_decode(erw, Elf_bswapuNN(itr->d_un.d_val)), str) == 0) {
        memset(itr, 0, sizeof(*itr));
        found = true;
      }
    }
  }
  if (found) {
    erw->modified |= ERW_MODIFIED_DHDRS;
    if (!erw->vers.count) erw_vers_init(erw);
    for (ver_group_t* g = erw->vers.need; g; g = g->next) {
      if (strcmp(erw_dynstr_decode(erw, g->name), str) == 0) {
        erw->modified |= ERW_MODIFIED_VERDEF | ERW_MODIFIED_VERNEED;
        g->state |= VER_EDIT_STATE_PENDING_DELETE;
        for (ver_item_t* i = g->items; i; i = i->next) {
          if (i->index < erw->vers.count && erw->vers.base[i->index].item == i) {
            add_verdef(erw, erw_dynstr_decode(erw, i->str), i->index);
          }
        }
      }
    }
  }
}

static void erwNNE_(dhdrs_ensure_minimal)(erw_state_t* erw) {
  uint32_t seen = 0;
  struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
  struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
  for (; itr != end; ++itr) {
    uint64_t tag = Elf_bswapuNN(itr->d_tag);
    if (tag <= 31) {
      seen |= 1u << tag;
    }
  }
  // glibc's dynamic loader assumes that DT_STRTAB and DT_SYMTAB are always present.
  if (!(seen & (1u << DT_STRTAB))) {
    erwNNE_(dhdrs_set_u)(erw, DT_STRTAB, erw_alloc(erw, erw_alloc_category_v_r, 1, 1));
    erwNNE_(dhdrs_set_u)(erw, DT_STRSZ, 1);
  }
  if (!(seen & (1u << DT_SYMTAB))) {
    erwNNE_(dhdrs_set_u)(erw, DT_SYMTAB, erw_alloc(erw, erw_alloc_category_v_r, sizeof(Elf_uNN), sizeof(struct ElfNN_(Sym))));
  }
}

#ifdef ERW_NATIVE_ENDIAN
static void erwNN_(dhdrs_fixup_nulls)(erw_state_t* erw) {
  // Remove all DT_NULL entries.
  struct ElfNN_(Dyn)* out = erw->dhdrs.base;
  struct ElfNN_(Dyn)* in = out;
  struct ElfNN_(Dyn)* end = in + erw->dhdrs.count;
  for (; in < end; ++in) {
    if (in->d_tag != DT_NULL) {
      memcpy(out++, in, sizeof(*in));
    }
  }
  // Add exactly one DT_NULL at the end.
  if (out >= (struct ElfNN_(Dyn)*)erw->dhdrs.base + (erw->dhdrs.capacity ? erw->dhdrs.capacity : erw->dhdrs.count)) {
    erw->dhdrs.count = out - (struct ElfNN_(Dyn)*)erw->dhdrs.base;
    erw_dhdrs_increase_capacity(erw);
    out = (struct ElfNN_(Dyn)*)erw->dhdrs.base + erw->dhdrs.count;
  }
  if (erw->dhdrs.capacity) {
    memset(out, 0, sizeof(*out));
    erw->dhdrs.count = (out + 1) - (struct ElfNN_(Dyn)*)erw->dhdrs.base;
  } else {
    // If editing in-place, also replace any excess entries with DT_NULL.
    memset(out, 0, (char*)((struct ElfNN_(Dyn)*)erw->dhdrs.base + erw->dhdrs.count) - (char*)out);
  }
}
#endif

static void erwNNE_(dhdrs_flush)(erw_state_t* erw) {
  size_t nb = erw->dhdrs.count * sizeof(struct ElfNN_(Dyn));
  uint64_t v = erw_alloc(erw, erw_alloc_category_v_rw, sizeof(Elf_uNN), nb);
  uint64_t f = v + v2f_map_lookup(&erw->v2f, v)->v2f;
  // Update any PT_DYNAMIC headers to point to new dhdrs.
  struct ElfNN_(Phdr)* itr = (struct ElfNN_(Phdr)*)erw->phdrs.base;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  bool any_dyn = false;
  for (; itr < end; ++itr) {
    if (Elf_bswapu32(itr->p_type) == PT_DYNAMIC) {
    populate_itr:
      if (erw->retry) {
        // Going around again? No need to actually flush the changes.
      } else {
        itr->p_offset = Elf_bswapuNN(f);
        itr->p_vaddr = itr->p_paddr = Elf_bswapuNN(v);
        itr->p_filesz = itr->p_memsz = Elf_bswapuNN(nb);
        itr->p_flags = Elf_bswapu32(PF_R | PF_W);
        itr->p_align = Elf_bswapuNN(sizeof(Elf_uNN));
        if (!any_dyn) {
          erwNNE_(shdr_update_from_phdr)(erw, ".dynamic", SHT_DYNAMIC, itr);
        }
      }
      any_dyn = true;
    }
  }
  if (!any_dyn) {
    // If there wasn't previously a PT_DYNAMIC, add one now.
    itr = erwNN_(phdrs_alloc)(erw);
    itr->p_type = Elf_bswapu32(PT_DYNAMIC);
    end = itr + 1;
    goto populate_itr;
  }
  if (erw->retry) {
    // Going around again? No need to actually flush the changes.
    return;
  }
  // Write out the new dhdrs.
  void* dst = (void*)(erw->f + f);
  memcpy(dst, erw->dhdrs.base, nb);
  free(erw->dhdrs.base);
  erw->dhdrs.base = dst;
  erw->dhdrs.capacity = 0;
  // TODO: Need to update _DYNAMIC symbol in symbol table? (see https://github.com/llvm/llvm-project/blob/main/lld/ELF/Writer.cpp)
  // TODO: Need to update GOT[0] ?
}

static reloc_editor_t* erwNNE_(init_reloc_editor)(reloc_editor_t* dst, erw_state_t* erw, struct ElfNN_(Dyn)* rel, struct ElfNN_(Dyn)* relsz, Elf_uNN entsz) {
  if (rel && relsz && entsz) {
    uint64_t v = Elf_bswapuNN(rel->d_un.d_ptr);
    v2f_entry_t* e = v2f_map_lookup(&erw->v2f, v);
    Elf_uNN count = Elf_bswapuNN(relsz->d_un.d_val) / entsz;
    uint64_t nb = (uint64_t)count * entsz;
    dst->count = count;
    dst->entry_size = entsz;
    if (!(e->v2f & V2F_SPECIAL) && (e[1].vbase - v) >= nb) {
      dst->base = erw->f + (v + e->v2f);
      dst->capacity = 0;
    } else {
      if (nb != (size_t)nb) {
        FATAL("Too many relocations for 32-bit program");
      }
      dst->base = malloc(nb);
      dst->capacity = count;
      materialise_v_range_to(erw, v, nb, dst->base);
    }
    ++dst;
  }
  return dst;
}

static reloc_editor_t* erwNNE_(relocs_init)(erw_state_t* erw) {
  struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
  struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
  struct ElfNN_(Dyn)* dt[32] = {0};
  struct ElfNN_(Dyn)* conflict = NULL;
  struct ElfNN_(Dyn)* conflictsz = NULL;
  for (; itr != end; ++itr) {
    Elf_uNN tag = Elf_bswapuNN(itr->d_tag);
    if (tag < 32) {
      dt[tag] = itr;
    } else {
      switch (tag) {
      case DT_GNU_CONFLICT: conflict = itr; break;
      case DT_GNU_CONFLICTSZ: conflictsz = itr; break;
      }
    }
  }
  reloc_editor_t* dst = erw->relocs;
  Elf_uNN relent = dt[DT_RELENT] ? Elf_bswapuNN(dt[DT_RELENT]->d_un.d_val) : sizeof(struct Elf64_Rel);
  Elf_uNN relaent = dt[DT_RELAENT] ? Elf_bswapuNN(dt[DT_RELAENT]->d_un.d_val) : sizeof(struct Elf64_Rela);
  if (dt[DT_REL]) {
    dst->dt = DT_REL;
    dst->flags = 0;
    dst = erwNNE_(init_reloc_editor)(dst, erw, dt[DT_REL], dt[DT_RELSZ], relent);
  }
  if (dt[DT_RELA]) {
    dst->dt = DT_RELA;
    dst->flags = RELOC_EDIT_FLAG_EXPLICIT_ADDEND;
    dst = erwNNE_(init_reloc_editor)(dst, erw, dt[DT_RELA], dt[DT_RELASZ], relaent);
  }
  if (conflict) {
    dst->dt = DT_GNU_CONFLICT & 0xff;
    dst->flags = RELOC_EDIT_FLAG_EXPLICIT_ADDEND;
    dst = erwNNE_(init_reloc_editor)(dst, erw, conflict, conflictsz, relaent);
  }
  if (dt[DT_PLTREL]) {
    switch (Elf_bswapuNN(dt[DT_PLTREL]->d_un.d_val)) {
    case DT_REL:
      dst->dt = DT_JMPREL;
      dst->flags = 0;
      dst = erwNNE_(init_reloc_editor)(dst, erw, dt[DT_JMPREL], dt[DT_PLTRELSZ], relent);
      break;
    case DT_RELA:
      dst->dt = DT_JMPREL;
      dst->flags = RELOC_EDIT_FLAG_EXPLICIT_ADDEND;
      dst = erwNNE_(init_reloc_editor)(dst, erw, dt[DT_JMPREL], dt[DT_PLTRELSZ], relaent);
      break;
    }
  }
  return dst;
}

static void erwNNE_(relocs_clear_rela_addends_for_type)(erw_state_t* erw, uint32_t type) {
  if (!erw->relocs[0].base) erw_relocs_init(erw);
  reloc_editor_t* editor = erw->relocs;
  for (; editor < erw->relocs + MAX_RELOC_EDITORS; ++editor) {
    if (!editor->base) break;
    if (!(editor->flags & RELOC_EDIT_FLAG_EXPLICIT_ADDEND)) continue;
    uint32_t esz = editor->entry_size;
    if (esz < sizeof(struct ElfNN_(Rela))) continue;
    uint32_t n = editor->count;
    char* ptr = editor->base;
    bool modified = false;
    for (; n; --n, ptr += esz) {
      struct ElfNN_(Rela)* rel = (struct ElfNN_(Rela)*)ptr;
      uint32_t r_info = (uint32_t)Elf_bswapuNN(rel->r_info);
#ifdef ERW_32
      r_info = (uint8_t)r_info;
#endif
      if (rel->r_addend != 0 && r_info == type) {
        rel->r_addend = 0;
        modified = true;
      }
    }
    if (modified) {
      if (editor->capacity) {
        editor->flags |= RELOC_EDIT_FLAG_DIRTY;
        erw->modified |= ERW_MODIFIED_RELOCS;
      } else {
        erw->modified |= ERW_MODIFIED_MISC;
      }
    }
  }
}

static reloc_editor_t* erwNNE_(relocs_prepare_add)(erw_state_t* erw, uint32_t section) {
  reloc_editor_t* editor = erw->relocs;
  for (; editor < erw->relocs + MAX_RELOC_EDITORS; ++editor) {
    if (editor->dt == 0) {
      editor->count = 0;
      editor->capacity = 1;
      editor->dt = (uint8_t)section;
      if (section == DT_RELA) {
        editor->flags = RELOC_EDIT_FLAG_EXPLICIT_ADDEND;
        editor->entry_size = sizeof(struct ElfNN_(Rela));
      } else {
        editor->flags = 0;
        editor->entry_size = sizeof(struct ElfNN_(Rel));
      }
      editor->base = malloc(editor->entry_size);
    } else if (editor->dt != (uint8_t)section) {
      continue;
    }
    erw->modified |= ERW_MODIFIED_RELOCS;
    editor->flags |= RELOC_EDIT_FLAG_DIRTY;
    if (editor->count >= editor->capacity) {
      erw_relocs_increase_capacity(editor);
    }
    return editor;
  }
  return NULL;
}

static bool erwNNE_(relocs_add)(erw_state_t* erw, uint32_t section, uint32_t kind, Elf_uNN where, uint32_t sym, Elf_uNN explicit_addend) {
  reloc_editor_t* editor = erwNNE_(relocs_prepare_add)(erw, section);
  if (!editor) return false;
  struct ElfNN_(Rela)* dst = (struct ElfNN_(Rela)*)((char*)editor->base + editor->entry_size * editor->count++);
  dst->r_offset = Elf_bswapuNN(where);
#ifdef ERW_32
  dst->r_info = Elf_bswapu32((uint8_t)kind + (sym << 24));
#else
  dst->r_info = Elf_bswapu64(kind + ((uint64_t)sym << 32));
#endif
  if (editor->flags & RELOC_EDIT_FLAG_EXPLICIT_ADDEND) {
    dst->r_addend = Elf_bswapuNN(explicit_addend);
  }
  return true;
}

static bool erwNNE_(relocs_add_early)(erw_state_t* erw, uint32_t section, uint32_t kind, Elf_uNN where, uint32_t sym, Elf_uNN explicit_addend) {
  reloc_editor_t* editor = erwNNE_(relocs_prepare_add)(erw, section);
  if (!editor) return false;
  size_t step = editor->entry_size;
  struct ElfNN_(Rela)* base = (struct ElfNN_(Rela)*)editor->base;
  struct ElfNN_(Rela)* dst = (struct ElfNN_(Rela)*)((char*)base + step * editor->count++);
  struct ElfNN_(Rela)* itr = dst;
  while (base < itr) {
    itr = (struct ElfNN_(Rela)*)((char*)itr - step);
    uint32_t info = (uint32_t)Elf_bswapuNN(itr->r_info);
#ifdef ERW_32
    info &= 0xff;
#endif
    if (info == kind) {
      memcpy(dst, itr, step);
      dst = itr;
    }
  }
  dst->r_offset = Elf_bswapuNN(where);
#ifdef ERW_32
  dst->r_info = Elf_bswapu32((uint8_t)kind + (sym << 24));
#else
  dst->r_info = Elf_bswapu64(kind + ((uint64_t)sym << 32));
#endif
  if (editor->flags & RELOC_EDIT_FLAG_EXPLICIT_ADDEND) {
    dst->r_addend = Elf_bswapuNN(explicit_addend);
  }
  return true;
}

static bool erwNNE_(relocs_expand_relr)(erw_state_t* erw) {
  uint64_t relr, relrsz;
  if (!erwNNE_(dhdrs_has)(erw, DT_RELR, &relr) || !erwNNE_(dhdrs_has)(erw, DT_RELRSZ, &relrsz) || !relrsz) {
    return true;
  }
  uint32_t section;
  Elf_uNN info;
  switch (erw->machine) {
  case EM_386: section = DT_REL; info = R_386_RELATIVE; break;
  case EM_ARM: section = DT_REL; info = R_ARM_RELATIVE; break;
  case EM_X86_64: section = DT_RELA; info = R_X86_64_RELATIVE; break;
  case EM_AARCH64: section = DT_RELA; info = R_AARCH64_RELATIVE; break;
  default: return false;
  }
  info = Elf_bswapuNN(info);

  relrsz = (relrsz + sizeof(Elf_uNN) - 1) & ~(uint64_t)(sizeof(Elf_uNN) - 1);
  v2f_entry_t* e = v2f_map_lookup(&erw->v2f, relr);
  Elf_uNN* relr_data;
  bool relr_data_malloced;
  if ((e->v2f & V2F_SPECIAL) || (e[1].vbase - relr) < relrsz) {
    relr_data = malloc(relrsz);
    relr_data_malloced = true;
    materialise_v_range_to(erw, relr, relrsz, (char*)relr_data);
  } else {
    relr_data = (Elf_uNN*)(erw->f + (relr + e->v2f));
    relr_data_malloced = false;
  }
  Elf_uNN* relr_data_end = (Elf_uNN*)((char*)relr_data + relrsz);

  // Count the number of relocations we'll need.
  uint32_t n_relocs = 0;
  for (Elf_uNN* itr = relr_data; itr < relr_data_end; ++itr) {
    Elf_uNN word = *itr;
    if (word & 1) {
      for (word >>= 1; word; word &= word - 1) {
        n_relocs += 1;
      }
    } else {
      n_relocs += 1;
    }
  }

  reloc_editor_t* editor = erwNNE_(relocs_prepare_add)(erw, section);
  if (!editor) {
    if (relr_data_malloced) free(relr_data);
    return false;
  }
  while ((editor->capacity - editor->count) < n_relocs) {
    erw_relocs_increase_capacity(editor);
  }

  // Write out new relocations.
  size_t dst_step = editor->entry_size;
  struct ElfNN_(Rela)* dst = (struct ElfNN_(Rela)*)((char*)editor->base + dst_step * editor->count);
  Elf_uNN r_addr = 0;
  editor->count += n_relocs;
  for (Elf_uNN* itr = relr_data; itr < relr_data_end; ++itr) {
    Elf_uNN word = *itr;
    Elf_uNN next_r_addr;
    if (word & 1) {
      next_r_addr = r_addr + (sizeof(Elf_uNN) * CHAR_BIT - 1) * sizeof(Elf_uNN);
      word >>= 1;
    } else {
      r_addr = word;
      next_r_addr = r_addr + sizeof(Elf_uNN);
      word = 1;
    }
    for (; word; word >>= 1) {
      if (word & 1) {
        dst->r_info = info;
        dst->r_offset = Elf_bswapuNN(r_addr);
        if (section == DT_RELA) {
          materialise_v_range_to(erw, r_addr, sizeof(Elf_uNN), (char*)&dst->r_addend);
        }
        dst = (struct ElfNN_(Rela)*)((char*)dst + dst_step);
      }
      r_addr += sizeof(Elf_uNN);
    }
    r_addr = next_r_addr;
  }

  if (relr_data_malloced) free(relr_data);
  return true;
}

static void erwNNE_(relocs_flush)(erw_state_t* erw) {
  // Prior to version 2.23 (in particular commit fa19d5c48a), glibc's dynamic
  // loader assumed that the payloads of DT_REL/DT_RELA (as appropriate for the
  // architecture in question) and DT_JMPREL were contiguous in memory, with
  // DT_JMPREL coming immediately after DT_REL/DT_RELA, with this assumption
  // being relied upon if LD_BIND_NOW (or an equivalent DT_FLAG) was set.
  // Accordingly, if we need to move any of DT_REL/DT_RELA/DT_JMPREL, then we
  // move all of them, and we allocate new virtual memory for DT_JMPREL after
  // allocating new virtual memory for DT_REL/DT_RELA.
  reloc_editor_t* visit_order[MAX_RELOC_EDITORS];
  uint32_t num_to_visit = 0;
  uint32_t num_to_force_move = 0;
  bool any_moved = false;
  for (int pass = 0; pass < 3; ++pass) {
    for (reloc_editor_t* editor = erw->relocs; editor < erw->relocs + MAX_RELOC_EDITORS; ++editor) {
      if (!editor->base) break;
      switch (editor->dt) {
      case DT_REL:
      case DT_RELA:
        if (pass != 0) continue;
        break;
      case DT_JMPREL:
        if (pass != 1) continue;
        break;
      default:
        if (pass != 2) continue;
        break;
      }
      if ((editor->flags & RELOC_EDIT_FLAG_DIRTY) && editor->capacity) {
        any_moved = true;
      }
      visit_order[num_to_visit++] = editor;
    }
    if (pass == 1 && any_moved) {
      num_to_force_move = num_to_visit;
    }
  }

  for (uint32_t i = 0; i < num_to_visit; ++i) {
    reloc_editor_t* editor = visit_order[i];
    if (!(editor->flags & RELOC_EDIT_FLAG_DIRTY) && i >= num_to_force_move) continue;
    size_t nb = editor->count * (size_t)editor->entry_size;
    if (editor->capacity || i < num_to_force_move) {
      const char* section = NULL;
      uint64_t v = erw_alloc(erw, erw_alloc_category_v_r, sizeof(Elf_uNN), nb);
      switch (editor->dt) {
      case DT_REL: erwNNE_(dhdrs_set_u)(erw, DT_REL, v); section = ".rel.dyn"; break;
      case DT_RELA: erwNNE_(dhdrs_set_u)(erw, DT_RELA, v); section = ".rela.dyn"; break;
      case DT_JMPREL: erwNNE_(dhdrs_set_u)(erw, DT_JMPREL, v); section = (editor->flags & RELOC_EDIT_FLAG_EXPLICIT_ADDEND) ? ".rela.plt" : ".rel.plt"; break;
      case DT_GNU_CONFLICT & 0xff: erwNNE_(dhdrs_set_u)(erw, DT_GNU_CONFLICT, v); break;
      }
      if (!erw->retry) {
        uint64_t f = v + v2f_map_lookup(&erw->v2f, v)->v2f;
        void* dst = (void*)(erw->f + f);
        memcpy(dst, editor->base, nb);
        if (editor->capacity) {
          free(editor->base);
        }
        editor->base = dst;
        editor->capacity = 0;
        if (section) {
          struct ElfNN_(Shdr)* shdr = erwNNE_(shdr_find)(erw, section);
          if (shdr) {
            shdr->sh_type = Elf_bswapu32((editor->flags & RELOC_EDIT_FLAG_EXPLICIT_ADDEND) ? SHT_RELA : SHT_REL);
            shdr->sh_flags = Elf_bswapuNN((Elf_bswapuNN(shdr->sh_flags) & ~(Elf_uNN)(SHF_WRITE | SHF_EXECINSTR)) | SHF_ALLOC);
            shdr->sh_addr = Elf_bswapuNN(v);
            shdr->sh_offset = Elf_bswapuNN(f);
            shdr->sh_size = Elf_bswapuNN(nb);
            shdr->sh_addralign = Elf_bswapuNN(sizeof(Elf_uNN));
          }
        }
      }
    }
    switch (editor->dt) {
    case DT_REL: erwNNE_(dhdrs_set_u)(erw, DT_RELSZ, nb); break;
    case DT_RELA: erwNNE_(dhdrs_set_u)(erw, DT_RELASZ, nb); break;
    case DT_JMPREL: erwNNE_(dhdrs_set_u)(erw, DT_PLTRELSZ, nb); break;
    case DT_GNU_CONFLICT & 0xff: erwNNE_(dhdrs_set_u)(erw, DT_GNU_CONFLICTSZ, nb); break;
    }
    if (editor->entry_size > 1) {
      erwNNE_(dhdrs_set_u)(erw, editor->flags & RELOC_EDIT_FLAG_EXPLICIT_ADDEND ? DT_RELAENT : DT_RELENT, editor->entry_size);
    }
  }
}

static uint32_t erwNNE_(symbol_count_from_gnu_hash)(erw_state_t* erw, uint64_t v) {
  uint32_t meta[4];
  materialise_v_range_to(erw, v, sizeof(meta), (char*)meta);
  v += sizeof(meta);
  v += Elf_bswapu32(meta[2]) * sizeof(Elf_uNN);
  uint64_t vend = v + Elf_bswapu32(meta[0]) * 4;
  uint32_t max_word = 0;
  if (v < vend) {
    v2f_entry_t* e = v2f_map_lookup(&erw->v2f, v);
    do {
      if (e->v2f & V2F_SPECIAL) {
        v = (++e)->vbase;
      } else {
        uint32_t* words = (uint32_t*)(erw->f + (v + e->v2f));
        uint64_t eend = (++e)->vbase;
        if (eend > vend) eend = vend;
        uint32_t* wend = (uint32_t*)((char*)words + (eend - v));
        v = eend;
        for (; words < wend; ++words) {
          uint32_t word = Elf_bswapu32(*words);
          if (word > max_word) max_word = word;
        }
      }
    } while (v < vend);
  }
  if (max_word) {
    v = vend + (max_word - Elf_bswapu32(meta[1])) * 4;
    v2f_entry_t* e = v2f_map_lookup(&erw->v2f, v);
    for (;;) {
      if (e->v2f & V2F_SPECIAL) {
        max_word += (v - e[1].vbase) / 4;
      } else {
        uint32_t* chain = (uint32_t*)(erw->f + (v + e->v2f));
        uint32_t* cend = (uint32_t*)((char*)chain + (e[1].vbase - v));
        while (chain < cend) {
          uint32_t cval = *chain++;
          ++max_word;
          if (cval & Elf_bswapu32(1)) {
            return max_word;
          }
        }
      }
      v = (++e)->vbase;
    }
  }
  return 0;
}

static uint32_t erwNNE_(symbol_count_from_relocs)(erw_state_t* erw, uint32_t nsym) {
  reloc_editor_t* editor = erw->relocs;
  reloc_kind_classifier classifier = reloc_kind_classifier_for_machine(erw->machine);
  for (; editor < erw->relocs + MAX_RELOC_EDITORS; ++editor) {
    if (!editor->base) break;
    uint32_t step = editor->entry_size;
    if (step < (sizeof(Elf_uNN) * 2)) continue;
    char* src = (char*)editor->base + sizeof(Elf_uNN);
    uint32_t count = editor->count;
    for (; count; --count, src += step) {
      Elf_uNN info = Elf_bswapuNN(*(Elf_uNN*)src);
#ifdef ERW_32
      uint32_t type = (uint8_t)info;
      info >>= 8;
#else
      uint32_t type = (uint32_t)info;
      info >>= 32;
#endif
      if (info >= nsym && classifier(type) > reloc_kind_no_symbol_lookup) nsym = info + 1;
    }
  }
  return nsym;
}

static void erwNNE_(gather_sym_info_from_relocs)(erw_state_t* erw) {
  sym_info_t* sym_info = erw->dsyms.info;
  reloc_editor_t* editor = erw->relocs;
  reloc_kind_classifier classifier = reloc_kind_classifier_for_machine(erw->machine);
  for (; editor < erw->relocs + MAX_RELOC_EDITORS; ++editor) {
    if (!editor->base) break;
    uint32_t step = editor->entry_size;
    if (step < (sizeof(Elf_uNN) * 2)) continue;
    char* src = (char*)editor->base + sizeof(Elf_uNN);
    uint32_t count = editor->count;
    if (!count) continue;
    uint16_t eager = (editor->dt == DT_JMPREL) ? SYM_INFO_RELOC_MAYBE_EAGER : SYM_INFO_RELOC_EAGER;
    for (; count; --count, src += step) {
      Elf_uNN info = Elf_bswapuNN(*(Elf_uNN*)src);
#ifdef ERW_32
      uint32_t type = (uint8_t)info;
      info >>= 8;
#else
      uint32_t type = (uint32_t)info;
      info >>= 32;
#endif
      switch (classifier(type)) {
      case reloc_kind_noop:
      case reloc_kind_no_symbol_lookup:
        break;
      case reloc_kind_lookup_result_unused:
      case reloc_kind_regular:
        sym_info[info].flags |= eager | SYM_INFO_RELOC_REGULAR;
        break;
      case reloc_kind_class_plt:
        sym_info[info].flags |= eager | SYM_INFO_RELOC_PLT;
        break;
      case reloc_kind_class_copy:
        sym_info[info].flags |= eager | SYM_INFO_RELOC_COPY;
        break;
      }
    }
  }
}

static void erwNNE_(gather_sym_info_from_dsym_table)(erw_state_t* erw) {
  sym_info_t* sym_info = erw->dsyms.info;
  uint32_t count = erw->dsyms.count;
  struct ElfNN_(Sym)* syms = erw->dsyms.base;
  uint32_t i;
  uint8_t undef_other = (erw->machine == EM_MIPS || erw->machine == EM_MIPS_RS3_LE) ? 0 : STO_MIPS_PLT;
  for (i = 0; i < count; ++i) {
    struct ElfNN_(Sym)* sym = syms + i;
    uint8_t stt = sym->st_info & 0xf;
    uint8_t bind = sym->st_info >> 4;
    uint8_t visi = sym->st_other & 3;
    uint32_t extra_flags = 0;
    if (bind == STB_LOCAL || visi == STV_HIDDEN || visi == STV_INTERNAL) {
      extra_flags |= SYM_INFO_LOCAL;
    } else if ((1 << stt) & ((1 << STT_NOTYPE) | (1 << STT_OBJECT) | (1 << STT_FUNC) | (1 << STT_COMMON) | (1 << STT_TLS) | (1 << STT_GNU_IFUNC))) {
      if (bind == STB_GLOBAL || bind == STB_WEAK || bind == STB_GNU_UNIQUE) {
        if (sym->st_value != 0 || sym->st_shndx == Elf_bswapu16(SHN_ABS) || stt == STT_TLS) {
          if (sym->st_shndx == SHN_UNDEF) {
            if ((sym->st_other | undef_other) & STO_MIPS_PLT) {
              extra_flags |= SYM_INFO_EXPORTABLE;
            }
          } else {
            extra_flags |= SYM_INFO_EXPORTABLE | SYM_INFO_PLT_EXPORTABLE;
          }
        }
      }
    }
    sym_info[i].flags |= extra_flags;
  }
}

static void erwNNE_(gather_sym_info_from_mips_got)(erw_state_t* erw) {
  struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
  struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
  Elf_uNN gotaddr = 0;
  Elf_uNN local_gotno = 0;
  Elf_uNN gotsym = 0;
  Elf_uNN symtabno = erw->dsyms.count;
  for (; itr != end; ++itr) {
    switch (Elf_bswapuNN(itr->d_tag)) {
    case DT_PLTGOT: gotaddr = Elf_bswapuNN(itr->d_un.d_ptr); break;
    case DT_MIPS_LOCAL_GOTNO: local_gotno = Elf_bswapuNN(itr->d_un.d_val); break;
    case DT_MIPS_SYMTABNO: symtabno = Elf_bswapuNN(itr->d_un.d_val); break;
    case DT_MIPS_GOTSYM: gotsym = Elf_bswapuNN(itr->d_un.d_val); break;
    }
  }
  gotaddr += local_gotno * sizeof(Elf_uNN);

  Elf_uNN gtmp;
  sym_info_t* sym_info = erw->dsyms.info;
  struct ElfNN_(Sym)* syms = erw->dsyms.base;
  for (; gotsym < symtabno; ++gotsym, gotaddr += sizeof(Elf_uNN)) {
    struct ElfNN_(Sym)* sym = syms + gotsym;
    uint8_t stt = sym->st_info & 0xf;
    uint32_t shndx = Elf_bswapu16(sym->st_shndx);
    uint32_t extra_flags = 0;
    if (shndx == SHN_UNDEF) {
      if (stt == STT_FUNC && sym->st_value && !(sym->st_other & STO_MIPS_PLT)) {
        extra_flags |= SYM_INFO_RELOC_MAYBE_EAGER | SYM_INFO_RELOC_PLT;
      } else {
        extra_flags |= SYM_INFO_RELOC_EAGER | SYM_INFO_RELOC_REGULAR;
      }
    } else if (shndx == SHN_COMMON) {
      extra_flags |= SYM_INFO_RELOC_EAGER | SYM_INFO_RELOC_REGULAR;
    } else if (stt == STT_FUNC && (materialise_v_range_to(erw, gotaddr, sizeof(gtmp), (char*)&gtmp), printf("XXX %d %d\n", (int)gtmp, (int)sym->st_value), gtmp != sym->st_value)) {
      extra_flags |= SYM_INFO_RELOC_MAYBE_EAGER | SYM_INFO_RELOC_PLT;
    } else if (stt != STT_SECTION) {
      extra_flags |= SYM_INFO_RELOC_EAGER | SYM_INFO_RELOC_REGULAR;
    }
    sym_info[gotsym].flags |= extra_flags;
  }
}

static void erwNNE_(gather_sym_info_from_hash)(erw_state_t* erw, uint64_t v) {
  struct ElfNN_(Sym)* syms = erw->dsyms.base;
  uint32_t meta[2];
  materialise_v_range_to(erw, v, sizeof(meta), (char*)meta);
  v += sizeof(meta);
  uint64_t v_chain = v + Elf_bswapu32(meta[0]) * sizeof(uint32_t);
  uint32_t i = Elf_bswapu32(meta[0]) + Elf_bswapu32(meta[1]);
  while (i--) {
    uint32_t b = materialise_v_u32(erw, v + i * sizeof(uint32_t));
    if (b) {
      // `b` contains a symbol index that _might_ be findable in the hash table
      // Perform a lookup to determine whether it is indeed findable.
      uint32_t h = erw_dynstr_decode_elf_hash(erw, Elf_bswapu32(syms[b].st_name));
      h = materialise_v_u32(erw, v + (h % Elf_bswapu32(meta[0])) * sizeof(uint32_t));
      uint32_t dist = 0x7fffffff;
      while (h) {
        if (h == b) {
          sym_info_t* sym_info = erw->dsyms.info + b;
          sym_info->flags |= SYM_INFO_IN_HASH;
          sym_info->hash_distance = dist;
          break;
        }
        --dist;
        h = materialise_v_u32(erw, v_chain + h * sizeof(uint32_t));
      }
    }
  }
}

static void erwNNE_(gather_sym_info_from_gnu_hash)(erw_state_t* erw, uint64_t v) {
  struct ElfNN_(Sym)* syms = erw->dsyms.base;
  uint32_t nsyms = erw->dsyms.count;
  uint32_t meta[4];
  materialise_v_range_to(erw, v, sizeof(meta), (char*)meta);
  v += sizeof(meta);
  uint32_t i = Elf_bswapu32(meta[1]);
  uint32_t bloom_mask = Elf_bswapu32(meta[2]) - 1;
  uint64_t v_buckets = v + Elf_bswapu32(meta[2]) * sizeof(Elf_uNN);
  uint64_t v_chain = v_buckets + Elf_bswapu32(meta[0]) * sizeof(uint32_t) - Elf_bswapu32(meta[1]) * sizeof(uint32_t);
  for (; i < nsyms; ++i) {
    // `i` contains a symbol index that _might_ be findable in the hash table
    // Perform a lookup to determine whether it is indeed findable.
    uint32_t h = erw_dynstr_decode_gnu_hash(erw, Elf_bswapu32(syms[i].st_name));
    Elf_uNN bitmask_word;
    materialise_v_range_to(erw, v + ((h / (sizeof(Elf_uNN) * CHAR_BIT)) & bloom_mask) * sizeof(Elf_uNN), sizeof(bitmask_word), (char*)&bitmask_word);
    bitmask_word = Elf_bswapuNN(bitmask_word);
    if (bitmask_word & ((Elf_uNN)1u << (h & (sizeof(Elf_uNN) * CHAR_BIT - 1)))) {
      if (bitmask_word & ((Elf_uNN)1u << ((h >> Elf_bswapu32(meta[3])) & (sizeof(Elf_uNN) * CHAR_BIT - 1)))) {
        uint32_t symix = materialise_v_u32(erw, v_buckets + (h % Elf_bswapu32(meta[0])) * sizeof(uint32_t));
        if (symix && symix <= i) {
          uint32_t symix0 = symix;
          h &=~ (uint32_t)1;
          do {
            uint32_t h2 = materialise_v_u32(erw, v_chain + symix * sizeof(uint32_t));
            h2 ^= h;
            if (symix == i && h2 <= 1) {
              sym_info_t* sym_info = erw->dsyms.info + i;
              sym_info->flags |= SYM_INFO_IN_GNU_HASH;
              sym_info->hash_distance = ~(symix - symix0);
              break;
            } else if (h2 & 1) {
              break;
            }
          } while (++symix <= i);
        }
      }
    }
  }
}

static void erwNNE_(dsyms_init)(erw_state_t* erw) {
  struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
  struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
  struct ElfNN_(Dyn)* symtab = NULL;
  struct ElfNN_(Dyn)* versym = NULL;
  struct ElfNN_(Dyn)* hash = NULL;
  struct ElfNN_(Dyn)* gnu_hash = NULL;
  struct ElfNN_(Dyn)* mips_symtabno = NULL;
  for (; itr != end; ++itr) {
    switch (Elf_bswapuNN(itr->d_tag)) {
    case DT_SYMTAB: symtab = itr; break;
    case DT_VERSYM: versym = itr; break;
    case DT_HASH: hash = itr; break;
    case DT_GNU_HASH: gnu_hash = itr; break;
    case DT_MIPS_SYMTABNO: mips_symtabno = itr; break;
    }
  }
  erw->dsyms.original_had_hash = erw->dsyms.want_hash = (hash != NULL);
  erw->dsyms.original_had_gnu_hash = erw->dsyms.want_gnu_hash = (gnu_hash != NULL);
  if (!symtab) { nosym:
    erw->dsyms.base = (void*)erw->f;
    erw->dsyms.count = erw->dsyms.capacity = 0;
    erw->dsyms.versions = NULL;
    return;
  }

  // Infer symbol table size from various sources
  uint32_t nsym = 0;
  if (mips_symtabno && (erw->machine == EM_MIPS || erw->machine == EM_MIPS_RS3_LE)) {
    uint32_t n = Elf_bswapuNN(mips_symtabno->d_un.d_val);
    if (n > nsym) nsym = n;
  }
  if (hash) {
    uint32_t nchain = materialise_v_u32(erw, Elf_bswapuNN(hash->d_un.d_ptr) + sizeof(uint32_t));
    if (nchain > nsym) nsym = nchain;
  }
  if (gnu_hash) {
    uint32_t n = erwNNE_(symbol_count_from_gnu_hash)(erw, Elf_bswapuNN(gnu_hash->d_un.d_ptr));
    erw->dsyms.original_gnu_hash_count = n;
    if (n > nsym) nsym = n;
  }
  if (!erw->relocs[0].base) erw_relocs_init(erw);
  nsym = erwNNE_(symbol_count_from_relocs)(erw, nsym);
  if (!nsym) {
    goto nosym;
  }

  // Now that size is known, can get a pointer to the memory range.
  uint64_t v = Elf_bswapuNN(symtab->d_un.d_ptr);
  v2f_entry_t* e = v2f_map_lookup(&erw->v2f, v);
  if (!(e->v2f & V2F_SPECIAL) && (e[1].vbase - v) >= nsym * sizeof(struct ElfNN_(Sym))) {
    erw->dsyms.base = (void*)(erw->f + (v + e->v2f));
    erw->dsyms.capacity = 0;
  } else {
    erw->dsyms.base = (void*)malloc(nsym * sizeof(struct ElfNN_(Sym)));
    erw->dsyms.capacity = nsym;
    materialise_v_range_to(erw, v, nsym * sizeof(struct ElfNN_(Sym)), (char*)erw->dsyms.base);
  }
  erw->dsyms.count = nsym;

  // Also get the memory range for the per-symbol version index.
  if (versym) {
    v = Elf_bswapuNN(versym->d_un.d_ptr);
    if (erw->dsyms.capacity) { materialise_versions:
      erw->dsyms.versions = (uint16_t*)malloc(nsym * sizeof(uint16_t));
      materialise_v_range_to(erw, v, nsym * sizeof(uint16_t), (char*)erw->dsyms.versions);
    } else {
      e = v2f_map_lookup(&erw->v2f, v);
      if ((e[1].vbase - v) >= nsym * sizeof(uint16_t)) {
        if (e->v2f & V2F_SPECIAL) {
          erw->dsyms.versions = NULL;
        } else {
          erw->dsyms.versions = (uint16_t*)(erw->f + (v + e->v2f));
        }
      } else {
        void* syms = malloc(nsym * sizeof(struct ElfNN_(Sym)));
        memcpy(syms, erw->dsyms.base, nsym * sizeof(struct ElfNN_(Sym)));
        erw->dsyms.base = syms;
        erw->dsyms.capacity = nsym;
        goto materialise_versions;
      }
    }
  } else {
    erw->dsyms.versions = NULL;
  }

  // Now gather symbol information.
  erw->dsyms.info = (sym_info_t*)calloc(nsym, sizeof(sym_info_t));
  erwNNE_(gather_sym_info_from_dsym_table)(erw);
  if (hash) erwNNE_(gather_sym_info_from_hash)(erw, Elf_bswapuNN(hash->d_un.d_ptr));
  if (gnu_hash) erwNNE_(gather_sym_info_from_gnu_hash)(erw, Elf_bswapuNN(gnu_hash->d_un.d_ptr));
  erwNNE_(gather_sym_info_from_relocs)(erw);
  if (erw->machine == EM_MIPS || erw->machine == EM_MIPS_RS3_LE) {
    erwNNE_(gather_sym_info_from_mips_got)(erw);
  }
}

static void erwNNE_(dsyms_clear_version)(erw_state_t* erw, sht_t* sym_names) {
  struct ElfNN_(Sym)* syms = erw->dsyms.base;
  uint32_t i, nsym = erw->dsyms.count;
  uint16_t* symvers = erw->dsyms.versions;
  for (i = 0; i < nsym; ++i) {
    uint16_t ver = Elf_bswapu16(symvers[i]) & 0x7fff;
    if (ver > 1) {
      struct ElfNN_(Sym)* sym = syms + i;
      const char* name = erw_dynstr_decode(erw, Elf_bswapu32(sym->st_name));
      if (sht_lookup_p(sym_names, name, strlen(name))) {
        symvers[i] = Elf_bswapu16((erw->dsyms.info[i].flags & SYM_INFO_LOCAL) ? 0 : 1);
        erw->modified |= ERW_MODIFIED_DSYMS;
        if (ver < erw->vers.count) {
          ver_index_ref_t* vref = erw->vers.base + ver;
          if (vref->item) {
            vref->item->state |= VER_EDIT_STATE_REMOVED_REFS;
            erw->modified |= vref->group ? ERW_MODIFIED_VERNEED : ERW_MODIFIED_VERDEF;
          }
        }
      }
    }
  }
}

static uint32_t erwNNE_(dsyms_find_or_add)(erw_state_t* erw, const char* name, uint16_t veridx, uint8_t stt) {
  struct ElfNN_(Sym)* syms = erw->dsyms.base;
  uint32_t nsym = erw->dsyms.count;
  uint16_t* symvers = erw->dsyms.versions;
  if (symvers != NULL || veridx <= 1) {
    // TODO: A cache could speed this up, but it might not actually matter.
    for (uint32_t i = 0; i < nsym; ++i) {
      if (symvers && ((symvers[i] ^ veridx) & Elf_bswapu16(0x7fff))) {
        continue;
      }
      struct ElfNN_(Sym)* sym = syms + i;
      uint8_t bind = sym->st_info >> 4;
      uint8_t visi = sym->st_other & 3;
      if (bind == STB_LOCAL || visi == STV_HIDDEN || visi == STV_INTERNAL) {
        continue;
      }
      if ((sym->st_info & 0xf) != stt) {
        continue;
      }
      if (strcmp(erw_dynstr_decode(erw, Elf_bswapu32(sym->st_name)), name) != 0) {
        continue;
      }
      return i;
    }
  }
  if (nsym >= erw->dsyms.capacity) {
    erw_dsyms_increase_capacity(erw);
    syms = erw->dsyms.base;
    symvers = erw->dsyms.versions;
  }
  if (nsym == 0) {
    memset(syms, 0, sizeof(*syms));
    if (symvers) *symvers = 0;
    erw->dsyms.info[0].flags = SYM_INFO_LOCAL;
    erw->dsyms.info[0].hash_distance = 0;
    erw->dsyms.count = nsym = 1;
  }
  struct ElfNN_(Sym)* sym = syms + nsym;
  sym->st_name = Elf_bswapu32(erw_dynstr_encode(erw, name));
  sym->st_value = 0;
  sym->st_size = 0;
  sym->st_info = (STB_GLOBAL << 4) + (stt & 0xf);
  sym->st_other = 0;
  sym->st_shndx = SHN_UNDEF;
  memset(erw->dsyms.info + nsym, 0, sizeof(erw->dsyms.info[0]));
  if (symvers) {
    symvers[nsym] = Elf_bswapu16(veridx);
  } else if (veridx > 1) {
    erw_dsyms_ensure_versions_array(erw);
    symvers = erw->dsyms.versions;
    symvers[nsym] = Elf_bswapu16(veridx);
  }
  erw->dsyms.count = nsym + 1;
  return nsym;
}

static int erwNNE_(dsym_sort_by_name) qsort_r_comparator(const void* lhs, const void* rhs, void* ctx) {
  erw_state_t* erw = (erw_state_t*)ctx;
  struct ElfNN_(Sym)* syms = erw->dsyms.base;
  uint32_t lhs_i = *(const uint32_t*)lhs;
  uint32_t rhs_i = *(const uint32_t*)rhs;
  const char* lhs_s0 = erw_dynstr_decode(erw, Elf_bswapu32(syms[lhs_i].st_name));
  const char* rhs_s0 = erw_dynstr_decode(erw, Elf_bswapu32(syms[rhs_i].st_name));
  const char* lhs_s = trim_leading_underscore(lhs_s0);
  const char* rhs_s = trim_leading_underscore(rhs_s0);
  int res = strcmp(lhs_s, rhs_s);
  if (res != 0) return res;
  res = strcmp(lhs_s0, rhs_s0);
  if (res != 0) return res;
  uint32_t lhs_d = erw->dsyms.info[lhs_i].hash_distance;
  uint32_t rhs_d = erw->dsyms.info[rhs_i].hash_distance;
  if (lhs_d != rhs_d) return lhs_d > rhs_d;
  if (lhs_i != rhs_i) return lhs_i < rhs_i ? -1 : 1;
  return 0;
}

void erwNNE_(dsyms_sort_indices)(erw_state_t* erw, uint32_t* indices, uint32_t count) {
  call_qsort_r(indices, count, sizeof(*indices), erwNNE_(dsym_sort_by_name), erw);
}

static void erwNNE_(rebuild_elf_hash)(erw_state_t* erw) {
  uint64_t v = erwNNE_(dhdrs_find_value)(erw, DT_HASH);
  uint32_t n_bucket;
  {
    uint32_t meta[2];
    if (v) {
      materialise_v_range_to(erw, v, sizeof(meta), (char*)meta);
#ifdef ERW_SWAP_ENDIAN
      meta[0] = Elf_bswapu32(meta[0]);
      meta[1] = Elf_bswapu32(meta[1]);
#endif
      if (meta[1] >= erw->dsyms.count) {
        n_bucket = meta[0] + meta[1] - erw->dsyms.count;
        goto got_v;
      }
    }
    n_bucket = sym_info_hash_count(erw, SYM_INFO_IN_HASH);
    n_bucket -= (n_bucket >> 2);
    n_bucket |= 1;
    if (v && (meta[0] + (uint64_t)meta[1]) >= (n_bucket + (uint64_t)erw->dsyms.count)) {
      n_bucket = meta[0] + meta[1] - erw->dsyms.count;
      goto got_v;
    } else {
      size_t nb = sizeof(uint32_t) * (2ull + n_bucket + erw->dsyms.count);
      v = erw_alloc(erw, erw_alloc_category_v_r, sizeof(uint32_t), nb);
      erwNNE_(dhdrs_set_u)(erw, DT_HASH, v);
      if (!erw->retry) {
        struct ElfNN_(Shdr)* shdr = erwNNE_(shdr_find)(erw, ".hash");
        if (shdr) {
          shdr->sh_type = Elf_bswapu32(SHT_HASH);
          shdr->sh_flags = Elf_bswapuNN((Elf_bswapuNN(shdr->sh_flags) & ~(Elf_uNN)(SHF_WRITE | SHF_EXECINSTR)) | SHF_ALLOC);
          shdr->sh_addr = Elf_bswapuNN(v);
          shdr->sh_offset = Elf_bswapuNN(v + v2f_map_lookup(&erw->v2f, v)->v2f);
          shdr->sh_size = Elf_bswapuNN(nb);
          shdr->sh_addralign = Elf_bswapuNN(sizeof(uint32_t));
        }
      }
    }
  }
got_v:
  if (erw->retry) return;
  uint32_t* meta = (uint32_t*)erw_view_v(erw, v);
  uint32_t* buckets = meta + 2;
  uint32_t* chains = buckets + n_bucket;
  memset(meta + 2, 0, (n_bucket + (uint64_t)erw->dsyms.count) * sizeof(uint32_t));
  uint32_t i, count = erw->dsyms.count;
  meta[0] = Elf_bswapu32(n_bucket);
  meta[1] = Elf_bswapu32(count);
  for (i = 0; i < count; ++i) {
    uint32_t flags = erw->dsyms.info[i].flags;
    if (!(flags & SYM_INFO_LOCAL) && (flags & SYM_INFO_EXPORTABLE) && (flags & SYM_INFO_IN_HASH)) {
      struct ElfNN_(Sym)* sym = (struct ElfNN_(Sym)*)erw->dsyms.base + i;
      uint32_t bucket = erw_dynstr_decode_elf_hash(erw, Elf_bswapu32(sym->st_name)) % n_bucket;
      uint32_t* where = buckets + bucket;
      uint32_t j;
      for (;;) {
        j = Elf_bswapu32(*where);
        if (j == 0) break;
        if (erw->dsyms.info[j].hash_distance <= erw->dsyms.info[i].hash_distance) break;
        where = chains + j;
      }
      chains[i] = Elf_bswapu32(j);
      *where = Elf_bswapu32(i);
    }
  }
}

#ifdef ERW_NATIVE_ENDIAN
static void erwNN_(dsyms_remap)(erw_state_t* erw, uint32_t* remap) {
  struct ElfNN_(Sym)* syms = erw->dsyms.base;
  sym_info_t* infos = erw->dsyms.info;
  uint16_t* vers = erw->dsyms.versions;
  for (uint32_t i = 0, count = erw->dsyms.count; i < count; ) {
    uint32_t j = remap[i];
    if (i == j) {
      ++i;
    } else {
#define swap(arr) {char buf[sizeof(*arr)]; memcpy(buf, arr + i, sizeof(buf)); memcpy(arr + i, arr + j, sizeof(buf)); memcpy(arr + j, buf, sizeof(buf)); }
      swap(remap);
      swap(syms);
      swap(infos);
      if (vers) {
        swap(vers);
      }
#undef swap
    }
  }
}
#endif

static void erwNNE_(relocs_remap)(erw_state_t* erw, uint32_t* remap) {
  reloc_editor_t* editor = erw->relocs;
  for (; editor < erw->relocs + MAX_RELOC_EDITORS; ++editor) {
    if (!editor->base) break;
    uint32_t step = editor->entry_size;
    if (step < (sizeof(Elf_uNN) * 2)) continue;
    char* src = (char*)editor->base + sizeof(Elf_uNN);
    uint32_t count = editor->count;
    bool modified = false;
    for (; count; --count, src += step) {
      Elf_uNN info = Elf_bswapuNN(*(Elf_uNN*)src);
#ifdef ERW_32
      uint32_t old_sym_index = info >> 8;
      uint32_t new_sym_index = remap[old_sym_index];
      if (old_sym_index != new_sym_index) {
        modified = true;
        info = (uint8_t)info | (new_sym_index << 8);
        *(Elf_uNN*)src = Elf_bswapuNN(info);
      }
#else
      uint32_t old_sym_index = info >> 32;
      uint32_t new_sym_index = remap[old_sym_index];
      if (old_sym_index != new_sym_index) {
        modified = true;
        info = (uint32_t)info | (((uint64_t)new_sym_index) << 32);
        *(Elf_uNN*)src = Elf_bswapuNN(info);
      }
#endif
    }
    if (modified) {
      erw->modified |= ERW_MODIFIED_RELOCS;
      editor->flags |= RELOC_EDIT_FLAG_DIRTY;
    }
  }
}

static void erwNNE_(rebuild_gnu_hash)(erw_state_t* erw) {
  uint32_t n_sym_in_hash = sym_info_hash_count(erw, SYM_INFO_IN_GNU_HASH);
  uint32_t n_bloom_log2 = bit_ceil_u32_log2(n_sym_in_hash * 8ull / (sizeof(Elf_uNN) * CHAR_BIT));
  uint32_t n_bloom = (uint32_t)1u << n_bloom_log2;
  uint32_t n_bucket = (n_sym_in_hash - (n_sym_in_hash >> 2)) | 1;
  uint64_t v = erwNNE_(dhdrs_find_value)(erw, DT_GNU_HASH);
  uint32_t new_u32_count = 4ull + n_bloom * (sizeof(Elf_uNN) / sizeof(uint32_t)) + n_bucket + n_sym_in_hash;
  if (v) {
    uint32_t meta[4];
    materialise_v_range_to(erw, v, sizeof(meta), (char*)meta);
    uint32_t old_u32_count = 4ull + Elf_bswapu32(meta[2]) * (sizeof(Elf_uNN) / sizeof(uint32_t)) + Elf_bswapu32(meta[0]) + (erw->dsyms.original_gnu_hash_count - Elf_bswapu32(meta[1]));
    if (old_u32_count >= new_u32_count) {
      n_bucket += (old_u32_count - new_u32_count);
      goto got_v;
    }
  }
  v = erw_alloc(erw, erw_alloc_category_v_r, sizeof(Elf_uNN), new_u32_count * sizeof(uint32_t));
  erwNNE_(dhdrs_set_u)(erw, DT_GNU_HASH, v);
  if (!erw->retry) {
    struct ElfNN_(Shdr)* shdr = erwNNE_(shdr_find)(erw, ".gnu.hash");
    if (shdr) {
      shdr->sh_type = Elf_bswapu32(SHT_GNU_HASH);
      shdr->sh_flags = (shdr->sh_flags & ~(Elf_uNN)(SHF_WRITE | SHF_EXECINSTR)) | SHF_ALLOC;
      shdr->sh_addr = Elf_bswapuNN(v);
      shdr->sh_offset = Elf_bswapuNN(v + v2f_map_lookup(&erw->v2f, v)->v2f);
      shdr->sh_size = Elf_bswapuNN(new_u32_count * sizeof(uint32_t));
      shdr->sh_addralign = Elf_bswapuNN(sizeof(Elf_uNN));
    }
  }
got_v:
  if (erw->retry) return;
  uint32_t* meta = (uint32_t*)erw_view_v(erw, v);
  meta[0] = Elf_bswapu32(n_bucket);
  meta[2] = Elf_bswapu32(n_bloom);
#ifdef ERW_32
  meta[3] = Elf_bswapu32(n_bloom_log2 > 22 ? 27 : 5 + n_bloom_log2);
#else
  meta[3] = Elf_bswapu32(n_bloom_log2 > 20 ? 26 : 6 + n_bloom_log2);
#endif
  Elf_uNN* bloom = (Elf_uNN*)(meta + 4);
  memset(bloom, 0, n_bloom * sizeof(*bloom));
  uint32_t* buckets = (uint32_t*)(bloom + n_bloom--);
  uint32_t i, count = erw->dsyms.count;
  uint32_t* bucket_counts = calloc(n_bucket, sizeof(uint32_t));
  uint32_t num_local = 0;
  for (i = 0; i < count; ++i) {
    uint32_t flags = erw->dsyms.info[i].flags;
    struct ElfNN_(Sym)* sym = (struct ElfNN_(Sym)*)erw->dsyms.base + i;
    if (!(flags & SYM_INFO_LOCAL) && (flags & SYM_INFO_EXPORTABLE) && (flags & SYM_INFO_IN_GNU_HASH)) {
      uint32_t hash = erw_dynstr_decode_gnu_hash(erw, Elf_bswapu32(sym->st_name));
      uint32_t bloom_idx = (hash / (sizeof(Elf_uNN) * CHAR_BIT)) & n_bloom;
      Elf_uNN bloom_word = Elf_bswapuNN(bloom[bloom_idx]);
      uint32_t bucket = hash % n_bucket;
      bloom_word |= ((Elf_uNN)1u << (hash & (sizeof(Elf_uNN) * CHAR_BIT - 1)));
      bloom_word |= ((Elf_uNN)1u << ((hash >> Elf_bswapu32(meta[3])) & (sizeof(Elf_uNN) * CHAR_BIT - 1)));
      bucket_counts[bucket] += 1;
      bloom[bloom_idx] = Elf_bswapuNN(bloom_word);
    } else if ((sym->st_info >> 4) == STB_LOCAL) {
      ++num_local;
    }
  }
  uint32_t total = count - n_sym_in_hash;
  uint32_t* chains = buckets + n_bucket - total;
  meta[1] = Elf_bswapu32(total);
  for (i = 0; i < n_bucket; ++i) {
    uint32_t here = bucket_counts[i];
    if (here) {
      buckets[i] = Elf_bswapu32(total);
      bucket_counts[i] = total;
      total += here;
    } else {
      buckets[i] = 0;
      bucket_counts[i] = count;
    }
  }
  if (total != count) {
    FATAL("Inconsistent logic between rebuild_gnu_hash and sym_info_hash_count");
  }
  uint32_t* remap = malloc(count * sizeof(uint32_t));
  total = num_local;
  num_local = 0;
  for (i = 0; i < count; ++i) {
    uint32_t flags = erw->dsyms.info[i].flags;
    struct ElfNN_(Sym)* sym = (struct ElfNN_(Sym)*)erw->dsyms.base + i;
    if (!(flags & SYM_INFO_LOCAL) && (flags & SYM_INFO_EXPORTABLE) && (flags & SYM_INFO_IN_GNU_HASH)) {
      uint32_t hash = erw_dynstr_decode_gnu_hash(erw, Elf_bswapu32(sym->st_name));
      uint32_t bucket = hash % n_bucket;
      uint32_t j = bucket_counts[bucket]++;
      chains[j] = Elf_bswapu32(hash &~ (uint32_t)1);
      remap[i] = j;
    } else if ((sym->st_info >> 4) == STB_LOCAL) {
      remap[i] = num_local++;
    } else {
      remap[i] = total++;
    }
  }
  for (i = 0; i < n_bucket; ++i) {
    if (buckets[i]) {
      *(chains + bucket_counts[i] - 1) |= Elf_bswapu32(1);
    }
  }
  free(bucket_counts);
  erwNNE_(relocs_remap)(erw, remap);
  erwNN_(dsyms_remap)(erw, remap);
  free(remap);
}

static bool erwNNE_(dsyms_move_locals_first)(erw_state_t* erw) {
  uint32_t count = erw->dsyms.count;
  struct ElfNN_(Sym)* syms = erw->dsyms.base;
  uint32_t num_local = 0;
  bool need_move = false;
  for (uint32_t i = 0; i < count; ++i) {
    if ((syms[i].st_info >> 4) == STB_LOCAL) {
      if (num_local != i) need_move = true;
      ++num_local;
    }
  }
  if (!need_move) return false;
  uint32_t* remap = malloc(count * sizeof(uint32_t));
  uint32_t not_local = num_local;
  num_local = 0;
  for (uint32_t i = 0; i < count; ++i) {
    remap[i] = ((syms[i].st_info >> 4) == STB_LOCAL) ? num_local++ : not_local++;
  }
  erwNNE_(relocs_remap)(erw, remap);
  erwNN_(dsyms_remap)(erw, remap);
  free(remap);
  return true;
}

static void erwNNE_(dsyms_fixup_sh_info)(erw_state_t* erw, struct ElfNN_(Shdr)* shdr) {
  uint32_t count = erw->dsyms.count;
  struct ElfNN_(Sym)* syms = erw->dsyms.base;
  uint32_t n_local = Elf_bswapu32(shdr->sh_info);
  while (n_local < count && (syms[n_local].st_info >> 4) == STB_LOCAL) {
    ++n_local;
  }
  shdr->sh_info = Elf_bswapu32(n_local);
}

static void erwNNE_(dsyms_flush)(erw_state_t* erw) {
  bool done_reorder = false;
  if (erw->dsyms.want_gnu_hash) {
    if (!erw->dsyms.original_had_gnu_hash || (erw->modified & ERW_MODIFIED_HASH_TABLES)) {
      erwNNE_(rebuild_gnu_hash)(erw); // NB: Can re-order symbols.
      erw->modified |= ERW_MODIFIED_HASH_TABLES; // Force rebuild of legacy hash table in case of symbol re-ordering.
      done_reorder = true;
    }
  } else if (erw->dsyms.original_had_gnu_hash) {
    erwNN_(dhdrs_remove)(erw, DT_GNU_HASH);
  }
  if (!done_reorder) {
    if (erwNNE_(dsyms_move_locals_first)(erw)) {
      if (!(erw->modified & ERW_MODIFIED_HASH_TABLES)) {
        erw->modified |= ERW_MODIFIED_HASH_TABLES;
        if (erw->dsyms.want_gnu_hash) {
          erwNNE_(rebuild_gnu_hash)(erw);
        }
      }
    }
  }
  if (erw->dsyms.want_hash) {
    if (!erw->dsyms.original_had_hash || (erw->modified & ERW_MODIFIED_HASH_TABLES)) {
      erwNNE_(rebuild_elf_hash)(erw);
    }
  } else if (erw->dsyms.original_had_hash) {
    erwNN_(dhdrs_remove)(erw, DT_HASH);
  }

  if (!erw->dsyms.count) {
    erwNN_(dhdrs_remove)(erw, DT_SYMTAB);
    erwNN_(dhdrs_remove)(erw, DT_VERSYM);
    return;
  }
  if (!erw->dsyms.capacity) {
    // All edits already done in-place; only things that might need
    // doing are erasing the version table and fixing up sh_info.
    if (!erw->dsyms.versions) {
      erwNN_(dhdrs_remove)(erw, DT_VERSYM);
    }
    struct ElfNN_(Shdr)* shdr = erwNNE_(shdr_find)(erw, ".dynsym");
    if (shdr) {
      erwNNE_(dsyms_fixup_sh_info)(erw, shdr);
    }
    return;
  }
  size_t nb1 = erw->dsyms.count * sizeof(struct ElfNN_(Sym));
  uint64_t v1 = erw_alloc(erw, erw_alloc_category_v_r, sizeof(Elf_uNN), nb1);
  uint64_t f1 = v1 + v2f_map_lookup(&erw->v2f, v1)->v2f;
  size_t nb2 = erw->dsyms.versions ? erw->dsyms.count * sizeof(uint16_t) : 0;
  uint64_t v2 = nb2 ? erw_alloc(erw, erw_alloc_category_v_r, sizeof(uint16_t), nb2) : 1;
  uint64_t f2 = v2 + v2f_map_lookup(&erw->v2f, v2)->v2f;
  erwNNE_(dhdrs_set_u)(erw, DT_SYMTAB, v1);
  if (nb2) {
    erwNNE_(dhdrs_set_u)(erw, DT_VERSYM, v2);
  } else {
    erwNN_(dhdrs_remove)(erw, DT_VERSYM);
  }
  if (!erw->retry) {
    void* d1 = erw->f + f1;
    memcpy(d1, erw->dsyms.base, nb1);
    free(erw->dsyms.base);
    erw->dsyms.base = d1;
    erw->dsyms.capacity = 0;
    if (nb2) {
      uint16_t* d2 = (uint16_t*)(erw->f + f2);
      memcpy(d2, erw->dsyms.versions, nb2);
      free(erw->dsyms.versions);
      erw->dsyms.versions = d2;
    }
    struct ElfNN_(Shdr)* shdr = erwNNE_(shdr_find)(erw, ".dynsym");
    if (shdr) {
      shdr->sh_type = Elf_bswapu32(SHT_DYNSYM);
      shdr->sh_flags = Elf_bswapuNN((Elf_bswapuNN(shdr->sh_flags) & ~(Elf_uNN)(SHF_WRITE | SHF_EXECINSTR)) | SHF_ALLOC);
      shdr->sh_addr = Elf_bswapuNN(v1);
      shdr->sh_offset = Elf_bswapuNN(f1);
      shdr->sh_size = Elf_bswapuNN(nb1);
      shdr->sh_addralign = Elf_bswapuNN(sizeof(Elf_uNN));
      erwNNE_(dsyms_fixup_sh_info)(erw, shdr);
    }
    if (nb2) {
      shdr = erwNNE_(shdr_find)(erw, ".gnu.version");
      if (shdr) {
        shdr->sh_type = Elf_bswapu32(SHT_GNU_versym);
        shdr->sh_flags = Elf_bswapuNN((Elf_bswapuNN(shdr->sh_flags) & ~(Elf_uNN)(SHF_WRITE | SHF_EXECINSTR)) | SHF_ALLOC);
        shdr->sh_addr = Elf_bswapuNN(v2);
        shdr->sh_offset = Elf_bswapuNN(f2);
        shdr->sh_size = Elf_bswapuNN(nb2);
        shdr->sh_addralign = Elf_bswapuNN(sizeof(uint16_t));
      }
    }
  }
}

static void erwNNE_(vers_init)(erw_state_t* erw) {
  struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
  struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
  struct ElfNN_(Dyn)* verneed = NULL;
  struct ElfNN_(Dyn)* verdef = NULL;
  for (; itr < end; ++itr) {
    switch (Elf_bswapuNN(itr->d_tag)) {
    case DT_VERNEED: verneed = itr; break;
    case DT_VERDEF: verdef = itr; break;
    }
  }
  if (verneed && verneed->d_un.d_val) {
    uint64_t vaddr = Elf_bswapuNN(verneed->d_un.d_val);
    ver_group_t** group_tail = &erw->vers.need;
    for (;;) {
      ver_group_t* g = *group_tail = (ver_group_t*)arena_alloc(&erw->arena, sizeof(ver_group_t));
      group_tail = &g->next;
      g->next = NULL;
      g->items = NULL;
      struct Elf_Verneed vn;
      g->vaddr = materialise_v_range_to(erw, vaddr, sizeof(vn), (char*)&vn) ? vaddr : 0;
      g->name = Elf_bswapu32(vn.vn_file);
      g->state = 0;
      if (vn.vn_aux) {
        uint64_t aaddr = vaddr + Elf_bswapu32(vn.vn_aux);
        ver_item_t** item_tail = &g->items;
        for (;;) {
          ver_item_t* i = *item_tail = (ver_item_t*)arena_alloc(&erw->arena, sizeof(ver_item_t));
          item_tail = &i->next;
          i->next = NULL;
          struct Elf_Vernaux vna;
          i->vaddr = materialise_v_range_to(erw, aaddr, sizeof(vna), (char*)&vna) ? aaddr : 0;
          i->str = Elf_bswapu32(vna.vna_name);
          i->hash = Elf_bswapu32(vna.vna_hash);
          i->flags = Elf_bswapu16(vna.vna_flags);
          i->index = Elf_bswapu16(vna.vna_other);
          i->state = 0;
          ver_index_ref_t* ref = erw_vers_ensure_index(erw, i->index & 0x7fff);
          ref->group = g;
          ref->item = i;
          if (!vna.vna_next) break;
          aaddr += Elf_bswapu32(vna.vna_next);
        }
      }
      if (!vn.vn_next) break;
      vaddr += Elf_bswapu32(vn.vn_next);
    }
  }
  if (verdef && verdef->d_un.d_val) {
    uint64_t vaddr = Elf_bswapuNN(verdef->d_un.d_val);
    ver_group_t** group_tail = &erw->vers.def;
    for (;;) {
      ver_group_t* g = *group_tail = (ver_group_t*)arena_alloc(&erw->arena, sizeof(ver_group_t) + sizeof(ver_item_t));
      group_tail = &g->next;
      g->next = NULL;
      ver_item_t* i = g->items = (ver_item_t*)(g + 1);
      g->name = 0;
      g->state = 0;
      i->next = NULL;
      i->vaddr = 0;
      i->str = 0;
      struct Elf_Verdef vd;
      g->vaddr = materialise_v_range_to(erw, vaddr, sizeof(vd), (char*)&vd) ? vaddr : 0;
      i->flags = Elf_bswapu16(vd.vd_flags);
      i->index = Elf_bswapu16(vd.vd_ndx);
      i->hash = Elf_bswapu32(vd.vd_hash);
      i->state = 0;
      if (vd.vd_aux) {
        uint64_t aaddr = vaddr + Elf_bswapu32(vd.vd_aux);
        if (!(i->flags & VER_FLG_BASE)) {
          ver_index_ref_t* ref = erw_vers_ensure_index(erw, i->index & 0x7fff);
          ref->group = NULL;
          ref->item = i;
        }
        for (;;) {
          struct Elf_Verdaux vda;
          i->vaddr = materialise_v_range_to(erw, aaddr, sizeof(vda), (char*)&vda) ? aaddr : 0;
          i->str = Elf_bswapu32(vda.vda_name);
          if (!vda.vda_next) break;
          aaddr += Elf_bswapu32(vda.vda_next);
          i = i->next = (ver_item_t*)arena_alloc(&erw->arena, sizeof(ver_item_t));
          i->next = NULL;
          i->flags = 0;
          i->index = 0;
          i->state = 0;
          i->hash = 0;
        }
      }
      if (!vd.vd_next) break;
      vaddr += Elf_bswapu32(vd.vd_next);
    }
  }
}

#if ERW_32
static void erwE_(vers_check_removed_refs)(erw_state_t* erw) {
  uint32_t nver = erw->vers.count;
  ver_index_ref_t* vers = erw->vers.base;
  uint16_t* versym = erw->dsyms.versions;
  uint32_t nsym = erw->dsyms.count;
  for (uint32_t i = 0; i < nsym; ++i) {
    uint16_t v = Elf_bswapu16(versym[i]) & 0x7fff;
    if (v < nver) {
      ver_item_t* item = vers[v].item;
      if (item) {
        uint8_t state = item->state;
        if (state & VER_EDIT_STATE_REMOVED_REFS) {
          item->state = state - VER_EDIT_STATE_REMOVED_REFS;
        }
      }
    }
  }
}
#endif

static void erwNNE_(vers_update_shdr_light)(erw_state_t* erw, const char* section_name, uint32_t g_count) {
  struct ElfNN_(Shdr)* shdr = erwNNE_(shdr_find)(erw, section_name);
  if (shdr) {
    shdr->sh_info = Elf_bswapu32(g_count);
  }
}

static void erwNNE_(vers_update_shdr_full)(erw_state_t* erw, const char* section_name, uint64_t v, size_t nb, uint32_t g_count) {
  struct ElfNN_(Shdr)* shdr = erwNNE_(shdr_find)(erw, section_name);
  if (shdr) {
    char n13 = section_name[13];
    if (n13 == 'r') {
      shdr->sh_type = Elf_bswapu32(SHT_GNU_verneed);
    } else if (n13 == 'd') {
      shdr->sh_type = Elf_bswapu32(SHT_GNU_verdef);
    } else {
      FATAL("Bad section name %s", section_name);
    }
    shdr->sh_flags = Elf_bswapuNN((Elf_bswapuNN(shdr->sh_flags) & ~(Elf_uNN)(SHF_WRITE | SHF_EXECINSTR)) | SHF_ALLOC);
    shdr->sh_addr = Elf_bswapuNN(v);
    shdr->sh_offset = Elf_bswapuNN(v + v2f_map_lookup(&erw->v2f, v)->v2f);
    shdr->sh_size = Elf_bswapuNN(nb);
    shdr->sh_info = Elf_bswapu32(g_count);
    shdr->sh_addralign = Elf_bswapuNN(4);
  }
}

static void erwNNE_(vers_flush)(erw_state_t* erw) {
  if (!erw->dsyms.base) erw_dsyms_init(erw);
  if (erw->dsyms.versions) {
    erwE_(vers_check_removed_refs)(erw);
  }
  if (erw->modified & ERW_MODIFIED_VERDEF) {
    verdef_sweep_removed_refs(erw->vers.def);
    vers_ensure_vaddrs(erw, erw->vers.def, sizeof(struct Elf_Verdef), sizeof(struct Elf_Verdaux), ".gnu.version_d");
    ver_group_t* g = erw->vers.def = vers_erase_pending_deletes(erw->vers.def);
    if (!g) {
      erwNN_(dhdrs_remove)(erw, DT_VERDEF);
      erwNN_(dhdrs_remove)(erw, DT_VERDEFNUM);
    } else {
      erwNNE_(dhdrs_set_u)(erw, DT_VERDEF, g->vaddr);
      erwNNE_(dhdrs_set_u)(erw, DT_VERDEFNUM, vers_g_count(g));
      if (!erw->retry) {
        while (g) {
          ver_item_t* i = g->items;
          ver_group_t* gn = g->next;
          if (g->state & VER_EDIT_STATE_DIRTY) {
            struct Elf_Verdef* vd = (struct Elf_Verdef*)erw_view_v(erw, g->vaddr);
            vd->vd_version = Elf_bswapu16(1);
            if (i) {
              vd->vd_flags = Elf_bswapu16(i->flags);
              vd->vd_ndx = Elf_bswapu16(i->index);
              vd->vd_cnt = Elf_bswapu16(vers_i_count(i));
              vd->vd_hash = Elf_bswapu32(i->hash);
              vd->vd_aux = Elf_bswapu32(i->vaddr - g->vaddr);
            } else {
              vd->vd_flags = 0;
              vd->vd_ndx = 0;
              vd->vd_cnt = 0;
              vd->vd_hash = 0;
              vd->vd_aux = 0;
            }
            vd->vd_next = gn ? Elf_bswapu32(gn->vaddr - g->vaddr) : 0;
          }
          while (i) {
            ver_item_t* in = i->next;
            if (i->state & VER_EDIT_STATE_DIRTY) {
              struct Elf_Verdaux* vda = (struct Elf_Verdaux*)erw_view_v(erw, i->vaddr);
              vda->vda_name = Elf_bswapu32(i->str);
              vda->vda_next = in ? Elf_bswapu32(in->vaddr - i->vaddr) : 0;
            }
            i = in;
          }
          g = gn;
        }
      }
    }
  }
  if (erw->modified & ERW_MODIFIED_VERNEED) {
    verneed_sweep_removed_refs(erw, erw->vers.need);
    vers_ensure_vaddrs(erw, erw->vers.need, sizeof(struct Elf_Verneed), sizeof(struct Elf_Vernaux), ".gnu.version_r");
    ver_group_t* g = erw->vers.need = vers_erase_pending_deletes(erw->vers.need);
    if (!g) {
      erwNN_(dhdrs_remove)(erw, DT_VERNEED);
      erwNN_(dhdrs_remove)(erw, DT_VERNEEDNUM);
    } else {
      erwNNE_(dhdrs_set_u)(erw, DT_VERNEED, g->vaddr);
      erwNNE_(dhdrs_set_u)(erw, DT_VERNEEDNUM, vers_g_count(g));
      if (!erw->retry) {
        while (g) {
          ver_item_t* i = g->items;
          ver_group_t* gn = g->next;
          if (g->state & VER_EDIT_STATE_DIRTY) {
            struct Elf_Verneed* vn = (struct Elf_Verneed*)erw_view_v(erw, g->vaddr);
            vn->vn_version = Elf_bswapu16(1);
            vn->vn_cnt = Elf_bswapu16(vers_i_count(i));
            vn->vn_file = Elf_bswapu32(g->name);
            vn->vn_aux = i ? Elf_bswapu32(i->vaddr - g->vaddr) : 0;
            vn->vn_next = gn ? Elf_bswapu32(gn->vaddr - g->vaddr) : 0;
          }
          while (i) {
            ver_item_t* in = i->next;
            if (i->state & VER_EDIT_STATE_DIRTY) {
              struct Elf_Vernaux* vna = (struct Elf_Vernaux*)erw_view_v(erw, i->vaddr);
              vna->vna_hash = Elf_bswapu32(i->hash);
              vna->vna_flags = Elf_bswapu16(i->flags);
              vna->vna_other = Elf_bswapu16(i->index);
              vna->vna_name = Elf_bswapu32(i->str);
              vna->vna_next = in ? Elf_bswapu32(in->vaddr - i->vaddr) : 0;
            }
            i = in;
          }
          g = gn;
        }
      }
    }
  }
  // If erasing needs and/or defs, might also need to erase the symbol version array.
  if (erw->dsyms.versions) {
    uint16_t max_index = 0;
    ver_group_t* g = erw->vers.need;
    for (; g; g = g->next) {
      ver_item_t* i = g->items;
      for (; i; i = i->next) {
        if (i->index > max_index) max_index = i->index;
      }
    }
    for (g = erw->vers.def; g; g = g->next) {
      ver_item_t* i = g->items;
      if (i && i->index > max_index) max_index = i->index;
    }
    if (!max_index) {
      if (erw->dsyms.capacity) {
        free(erw->dsyms.versions);
      }
      erw->dsyms.versions = NULL;
      erw->modified |= ERW_MODIFIED_DSYMS;
    } else {
      // Check for any symbols pointing at out of range versions, and repoint to
      // global version. This shouldn't happen, but is a nice safety feature.
      uint16_t* versym = erw->dsyms.versions;
      uint32_t nsym = erw->dsyms.count;
      uint32_t i;
      for (i = 0; i < nsym; ++i) {
        uint16_t v = Elf_bswapu16(versym[i]) & 0x7fff;
        if (v > max_index) {
          versym[i] = Elf_bswapu16(1);
          erw->modified |= ERW_MODIFIED_DSYMS;
        }
      }
    }
  }
}

static void erwNNE_(dynstr_init)(erw_state_t* erw) {
  struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
  struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
  for (; itr != end; ++itr) {
    if (Elf_bswapuNN(itr->d_tag) == DT_STRTAB) {
      erw->dynstr.original_base = Elf_bswapuNN(itr->d_un.d_ptr);
    }
  }
}

static void erwNNE_(dynstr_visit_refs)(erw_state_t* erw, void* ctx, uint64_t (*fn)(void*, uint64_t)) {
  uint32_t modified = 0;
  { // Visit zero if it represents an empty string.
    uint64_t addr = erw->dynstr.original_base;
    v2f_entry_t* e = v2f_map_lookup(&erw->v2f, addr);
    if ((e->v2f & V2F_SPECIAL) || !erw->f[addr + e->v2f]) {
      fn(ctx, 0);
    }
  }
  { // Visit references in dhdrs
    if (!erw->dhdrs.base) erw_dhdrs_init(erw);
    struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
    struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
    struct ElfNN_(Dyn)* liblist = NULL;
    struct ElfNN_(Dyn)* liblistsz = NULL;
    for (; itr != end; ++itr) {
      switch (Elf_bswapuNN(itr->d_tag)) {
      case DT_GNU_LIBLIST:
        liblist = itr;
        break;
      case DT_GNU_LIBLISTSZ:
        liblistsz = itr;
        break;
      case DT_STRSZ: {
        // Visit end of string table, so that the original size is known.
        uint64_t oval = Elf_bswapuNN(itr->d_un.d_val);
        if (oval) {
          fn(ctx, oval - 1);
        }
        break; }
      case DT_NEEDED:
      case DT_SONAME:
      case DT_RPATH:
      case DT_RUNPATH:
      case DT_DEPAUDIT:
      case DT_AUDIT:
      case DT_AUXILIARY:
      case DT_FILTER: {
        uint64_t oval = Elf_bswapuNN(itr->d_un.d_val);
        uint64_t nval = fn(ctx, oval);
        if (oval != nval) {
          modified |= ERW_MODIFIED_DHDRS;
          itr->d_un.d_val = Elf_bswapuNN(nval);
        }
        break; }
      }
    }
    if (liblist && liblistsz) {
      uint64_t v = Elf_bswapuNN(liblist->d_un.d_ptr);
      uint64_t vend = v + Elf_bswapuNN(liblistsz->d_un.d_val);
      vend -= 3;
      if (v < vend) {
        v2f_entry_t* e = v2f_map_lookup(&erw->v2f, v);
        do {
          uint64_t eend = e[1].vbase;
          if (eend > vend) eend = vend;
          if (e->v2f & V2F_SPECIAL) {
            while (v < eend) {
              v += 20;
            }
          } else {
            uint8_t* base = erw->f + e->v2f;
            while (v < eend) {
              uint32_t oval = Elf_bswapu32(*(uint32_t*)(base + v));
              uint32_t nval = fn(ctx, oval);
              if (oval != nval) {
                modified |= ERW_MODIFIED_MISC;
                *(uint32_t*)(base + v) = Elf_bswapu32(nval);
              }
              v += 20;
            }
          }
          ++e;
        } while (v < vend);
      }
    }
  }
  { // Visit references in dsyms
    if (!erw->dsyms.base) erw_dsyms_init(erw);
    struct ElfNN_(Sym)* itr = erw->dsyms.base;
    struct ElfNN_(Sym)* end = itr + erw->dsyms.count;
    for (; itr != end; ++itr) {
      uint32_t oval = Elf_bswapu32(itr->st_name);
      uint32_t nval = fn(ctx, oval);
      if (oval != nval) {
        modified |= ERW_MODIFIED_DSYMS;
        itr->st_name = Elf_bswapu32(nval);
      }
    }
  }
  { // Visit references in verneed / verdef
    if (!erw->vers.count) erw_vers_init(erw);
    ver_group_t* g = erw->vers.need;
    for (; g; g = g->next) {
      if (g->state & VER_EDIT_STATE_PENDING_DELETE) continue;
      uint32_t nval = fn(ctx, g->name);
      if (nval != g->name) {
        modified |= ERW_MODIFIED_VERNEED;
        g->name = nval;
        g->state |= VER_EDIT_STATE_DIRTY;
      }
      ver_item_t* i = g->items;
      for (; i; i = i->next) {
        if (i->state & VER_EDIT_STATE_PENDING_DELETE) continue;
        nval = fn(ctx, i->str);
        if (nval != i->str) {
          modified |= ERW_MODIFIED_VERNEED;
          i->str = nval;
          i->state |= VER_EDIT_STATE_DIRTY;
        }
      }
    }
    for (g = erw->vers.def; g; g = g->next) {
      if (g->state & VER_EDIT_STATE_PENDING_DELETE) continue;
      ver_item_t* i = g->items;
      for (; i; i = i->next) {
        if (i->state & VER_EDIT_STATE_PENDING_DELETE) continue;
        uint32_t nval = fn(ctx, i->str);
        if (nval != i->str) {
          modified |= ERW_MODIFIED_VERDEF;
          i->str = nval;
          i->state |= VER_EDIT_STATE_DIRTY;
        }
      }
    }
  }
  erw->modified |= modified;
}

static void erwNNE_(dynstr_flush)(erw_state_t* erw) {
  uint64_t v = erw_alloc(erw, erw_alloc_category_v_r, 1, erw->dynstr.to_add_size);
  uint64_t f = v + v2f_map_lookup(&erw->v2f, v)->v2f;
  if (v) {
    memcpy(erw->f + f, erw->dynstr.to_add, erw->dynstr.to_add_size);
  }
  erwNNE_(dhdrs_set_u)(erw, DT_STRTAB, v);
  erwNNE_(dhdrs_set_u)(erw, DT_STRSZ, erw->dynstr.to_add_size);
  if (!erw->retry) {
    struct ElfNN_(Shdr)* shdr = erwNNE_(shdr_find)(erw, ".dynstr");
    if (shdr) {
      shdr->sh_type = Elf_bswapu32(SHT_STRTAB);
      shdr->sh_flags = Elf_bswapuNN((Elf_bswapuNN(shdr->sh_flags) & ~(Elf_uNN)(SHF_WRITE | SHF_EXECINSTR)) | SHF_ALLOC);
      shdr->sh_addr = Elf_bswapuNN(v);
      shdr->sh_offset = Elf_bswapuNN(f);
      shdr->sh_size = Elf_bswapuNN(erw->dynstr.to_add_size);
      shdr->sh_addralign = Elf_bswapuNN(1);
    }
  }
}

static void erwNNE_(retry)(erw_state_t* erw) {
  // Reset phdrs, and ensure space for new PT_LOADs.
  uint32_t phdr_capacity = erw->phdrs.count = erw->original->phdr_count;
  phdr_capacity += extra_phdrs_required_for_category_mask(erw->alloc_buckets.category_mask);
  if (erw->phdrs.capacity < phdr_capacity) {
    if (erw->phdrs.capacity) free(erw->phdrs.base);
    erw->phdrs.base = malloc(sizeof(struct ElfNN_(Phdr)) * phdr_capacity);
    erw->phdrs.capacity = phdr_capacity;
  }
  memcpy(erw->phdrs.base, erw->original + 1, sizeof(struct ElfNN_(Phdr)) * erw->phdrs.count);

  qsort(erw->alloc_buckets.base, erw->alloc_buckets.count, sizeof(erw_alloc_bucket_t), cmp_alloc_bucket);

  // Determine where to start allocating new f and new v at.
  uint64_t page_size = erw->guest_page_size;
  uint64_t f = 0;
  uint64_t v = 0;
  {
    struct ElfNN_(Phdr)* itr = erw->phdrs.base;
    struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
    for (; itr != end; ++itr) {
      if (Elf_bswapu32(itr->p_type) != PT_LOAD) continue;
      Elf_uNN itr_f = Elf_bswapuNN(itr->p_offset) + Elf_bswapuNN(itr->p_filesz);
      Elf_uNN itr_v = Elf_bswapuNN(itr->p_vaddr) + Elf_bswapuNN(itr->p_memsz);
      Elf_uNN itr_a = Elf_bswapuNN(itr->p_align);
      if (f < itr_f) f = itr_f;
      if (v < itr_v) v = itr_v;
      if (itr_a > page_size && itr_a <= 0x10000 && !(itr_a & (itr_a - 1))) page_size = itr_a;
    }
  }
  f = (f + page_size - 1) &- page_size;
  v = (v + page_size - 1) &- page_size;
  if (f < erw->original->f_size) f = erw->original->f_size;

  // Dish out new ranges
  erw_alloc_bucket_t* itr = erw->alloc_buckets.base;
  erw_alloc_bucket_t* end = itr + erw->alloc_buckets.count;
  struct ElfNN_(Phdr)* ld = NULL;
  for (; itr != end; ++itr) {
    uint64_t size = (itr->current > itr->end ? itr->current : itr->end) - itr->start;
    if (itr->category == erw_alloc_category_f) {
      f = (f + itr->alignment - 1) &- (uint64_t)itr->alignment;
      itr->start = itr->current = f;
      f += size;
      itr->end = f;
    } else {
      uint32_t flags = PF_R;
      if (itr->category == erw_alloc_category_v_rx) flags |= PF_X;
      if (itr->category == erw_alloc_category_v_rw || itr->category == erw_alloc_category_v_rw_zero) flags |= PF_W;
      if (!ld || ld->p_flags != flags) {
        struct ElfNN_(Phdr)* first_load = NULL;
        ld = ld ? ld + 1 : (struct ElfNN_(Phdr)*)erw->phdrs.base + erw->phdrs.count;
        ld->p_type = Elf_bswapu32(PT_LOAD);
        ld->p_flags = Elf_bswapu32(flags);
        ld->p_align = Elf_bswapuNN(page_size);
        if (flags == PF_R) {
          // Related to erw_phdrs_bug_workaround, the new PT_LOAD which covers the new
          // program headers needs to have alignment at least that of the first PT_LOAD.
          first_load = erwNN_(phdrs_find_first)(erw, PT_LOAD);
          if (first_load && Elf_bswapuNN(first_load->p_align) > Elf_bswapuNN(ld->p_align)) {
            ld->p_align = Elf_bswapuNN(first_load->p_align);
          }
        }
        v = (v + page_size - 1) &- page_size;
        {
          // v needs to be congruent to f, modulo ld->p_align
          uint64_t vf = (v &- Elf_bswapuNN(ld->p_align)) + (f & (Elf_bswapuNN(ld->p_align) - 1));
          if (vf < v) vf += Elf_bswapuNN(ld->p_align);
          v = vf;
        }
        if (first_load) {
          uint64_t required_p_offset = Elf_bswapuNN(first_load->p_vaddr) + f - v;
          if (Elf_bswapuNN(first_load->p_offset) != (Elf_uNN)required_p_offset && (int64_t)required_p_offset < 0) {
            // As things stand, erw_phdrs_bug_workaround would end up setting p_offset
            // to a negative value, which can upset glibc's ET_DYN loader. We can fix
            // this by increasing f.
            required_p_offset &= -page_size;
            f -= required_p_offset;
            // Then potentially increase f even more to restore congruence.
            uint64_t vf = (f &- Elf_bswapuNN(ld->p_align)) + (v & (Elf_bswapuNN(ld->p_align) - 1));
            if (vf < f) vf += Elf_bswapuNN(ld->p_align);
            f = vf;
            // TODO: This gap in f could be filled by later alloc categories.
          } else if (Elf_bswapuNN(first_load->p_offset) < (Elf_uNN)required_p_offset && first_load->p_filesz != 0) {
            Elf_uNN excess = (Elf_uNN)required_p_offset - Elf_bswapuNN(first_load->p_offset);
            if (excess <= 0x40000 && !(excess & (page_size - 1))) {
              // As things stand, erw_phdrs_bug_workaround will need to insert a
              // dummy PT_LOAD, but we can burn a small amount of virtual address
              // space in exchange for avoiding the dummy. 
              v += excess;
            }
          }
        }
        uint32_t align_bump = (itr->alignment - (uint32_t)v) & (itr->alignment - 1);
        v += align_bump;
        f += align_bump;
        if (f == v && ld == erw->phdrs.base && flags == PF_R) {
          // This is the first PT_LOAD in a file which originally had no PT_LOAD.
          // Extend this segment to the left, so that the ELF header gets mapped.
          ld->p_offset = 0;
          ld->p_vaddr = ld->p_paddr = 0;
        } else {
          ld->p_offset = Elf_bswapuNN(f);
          ld->p_vaddr = ld->p_paddr = Elf_bswapuNN(v);
        }
      }
      // A little bit of v.
      uint32_t align_bump = (itr->alignment - (uint32_t)v) & (itr->alignment - 1);
      v += align_bump;
      itr->start = itr->current = v;
      v += size;
      itr->end = v;
      ld->p_memsz = Elf_bswapuNN(v - Elf_bswapuNN(ld->p_vaddr));
      // Corresponding bit of f.
      if (itr->category != erw_alloc_category_v_rw_zero) {
        f += align_bump + size;
      }
      ld->p_filesz = Elf_bswapuNN(f - Elf_bswapuNN(ld->p_offset));
    }
  }
  if (ld) {
    erw->phdrs.count = (ld + 1) - (struct ElfNN_(Phdr)*)erw->phdrs.base;
  }
  erw->f_size = f;
  v2f_map_init(&erw->v2f, V2F_UNMAPPED);
  erwNNE_(v2f_init)(erw);

  // Reset all the editors (except the phdr editor).
  free_editors_common(erw);
  erw->retry = false;
  arena_reset(&erw->arena);

  // Reset file contents.
  erw_reinit_f(erw);
  if ((erw->alloc_buckets.category_mask & (1u << erw_alloc_category_v_rx)) && (erw->machine == EM_386 || erw->machine == EM_X86_64)) {
    // Initialise newly allocated executable memory to 0xCC
    ld = erw->phdrs.base;
    while (Elf_bswapuNN(ld->p_offset) < erw->original->f_size || !(Elf_bswapu32(ld->p_flags) & PF_X)) {
      ++ld;
    }
    memset(erw->f + Elf_bswapuNN(ld->p_offset), 0xCC, Elf_bswapuNN(ld->p_filesz));
  }

  // If modifying dynstrs, initialise the editor directly into editing mode
  // now, so that all pre-existing strings get picked up.
  if (erw->modified & ERW_MODIFIED_DYNSTR) {
    switch (erw->dynstr.state) {
    case dynstr_edit_state_uninit:
      erw_dynstr_init(erw);
      // fallthrough
    case dynstr_edit_state_read_only:
      erw_dynstr_build_table(erw);
      // fallthrough
    case dynstr_edit_state_encode_from_existing:
      erw_dynstr_start_building_new(erw);
      // fallthrough
    case dynstr_edit_state_encode_building_new:
      break;
    }
  }
}

static bool erwNNE_(flush)(erw_state_t* erw) {
  if (erw->modified & (ERW_MODIFIED_VERDEF | ERW_MODIFIED_VERNEED)) {
    // Before dhdrs.
    // Before dsyms, as can remove the per-symbol version array.
    erwNNE_(vers_flush)(erw);
  }
  if (erw->modified & ERW_MODIFIED_DSYMS) {
    // Before relocs, as can re-order the symbol table, therein updating relocs.
    // Before dhdrs.
    erwNNE_(dsyms_flush)(erw);
  }
  if (erw->modified & ERW_MODIFIED_DYNSTR) {
    // Before dhdrs.
    erwNNE_(dynstr_flush)(erw);
  }
  if (erw->modified & ERW_MODIFIED_RELOCS) {
    // Before dhdrs.
    erwNNE_(relocs_flush)(erw);
  }
  if (erw->modified & ERW_MODIFIED_DHDRS) {
    if (erw->dhdrs.capacity) {
      erwNNE_(dhdrs_ensure_minimal)(erw);
    }
    erwNN_(dhdrs_fixup_nulls)(erw);
    if (erw->dhdrs.capacity) {
      erwNNE_(dhdrs_flush)(erw);
    }
  }
  if (erw->phdrs.capacity || (erw->alloc_buckets.category_mask & ~(1u << erw_alloc_category_f))) {
    erwNNE_(phdrs_flush)(erw);
  }
  if (erw->retry) {
    erwNNE_(retry)(erw);
    return false;
  }
  if (erw->modified & ERW_MODIFIED_SHDRS) {
    erwNNE_(shdr_fixup_section_symbols)(erw);
  }
  return true;
}
