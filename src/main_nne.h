#if defined(ERW_32) && defined(ERW_NATIVE_ENDIAN)
static void print_interpreter_common(erw_state_t* erw, uint64_t off, uint64_t len) {
  if (off > erw->f_size) {
    printf("PT_INTERP has p_offset %llu, which is greater than file size %llu\n", (long long unsigned)off, (long long unsigned)erw->f_size);
  } else {
    const char* str = (const char*)(erw->f + off);
    uint64_t avail = erw->f_size - off;
    uint64_t num_oob = 0;
    if (avail < len) {
      num_oob = len - avail;
      len = avail;
    }
    bool got_nul = false;
    if (len != 0 && (str + len)[-1] == '\0') {
      got_nul = true;
      --len;
    }
    if (len) {
      fwrite(str, 1, len, stdout);
    }
    if (!got_nul) {
      puts(" (missing NUL terminator)");
    }
    if (num_oob) {
      printf(" (%llu more characters beyond end of file)", (long long unsigned)num_oob);
    }
    putchar('\n');
  }
}
#endif

static void erwNNE_(print_interpreter)(erw_state_t* erw, void* itr_) {
  struct ElfNN_(Phdr)* itr = (struct ElfNN_(Phdr)*)itr_;
  print_interpreter_common(erw, Elf_bswapuNN(itr->p_offset), Elf_bswapuNN(itr->p_filesz));
}

static uint32_t erwNNE_(get_gnu_stack_flags)(erw_state_t* erw) {
  uint32_t stack_flags = PF_X;
  struct ElfNN_(Phdr)* itr = (struct ElfNN_(Phdr)*)erw->phdrs.base;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  for (; itr != end; ++itr) {
    if (Elf_bswapu32(itr->p_type) == PT_GNU_STACK) {
      stack_flags = Elf_bswapu32(itr->p_flags);
    }
  }
  return stack_flags;
}

static void erwNNE_(print_imported_libs)(erw_state_t* erw) {
  struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
  struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
  for (; itr != end; ++itr) {
    const char* extra = NULL;
    switch (Elf_bswapuNN(itr->d_tag)) {
      case DT_NEEDED: extra = ""; break;
      case DT_AUXILIARY: extra = " (filter, weak)"; break;
      case DT_FILTER: extra = " (filter)"; break;
      default: continue;
    }
    printf("library  %s%s\n", erw_dynstr_decode(erw, Elf_bswapuNN(itr->d_un.d_val)), extra);
  }
}

static void erwNNE_(action_print_exports)(erw_state_t* erw) {
  {
    if (!erw->dhdrs.base) erw_dhdrs_init(erw);
    struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
    struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
    struct ElfNN_(Dyn)* soname = NULL;
    bool any_libs = false;
    for (; itr != end; ++itr) {
      switch (Elf_bswapuNN(itr->d_tag)) {
      case DT_SONAME: soname = itr; break;
      case DT_NEEDED:
      case DT_AUXILIARY:
      case DT_FILTER:
        any_libs = true;
        break;
      }
    }
    if (soname) {
      printf("soname %s\n", erw_dynstr_decode(erw, Elf_bswapuNN(soname->d_un.d_val)));
    }
    if (any_libs) {
      erwNNE_(print_imported_libs)(erw);
    }
  }
  {
    if (!erw->vers.count) erw_vers_init(erw);
    for (ver_group_t* g = erw->vers.def; g; g = g->next) {
      ver_item_t* i = g->items;
      if (i) {
        printf("version  %s\n", erw_dynstr_decode(erw, i->str));
      }
    }
  }
  {
    if (!erw->dsyms.base) erw_dsyms_init(erw);
    uint32_t count = erw->dsyms.count;
    uint32_t* indices = (uint32_t*)malloc(count * sizeof(uint32_t));
    uint16_t hash_bits = (erw->dsyms.want_hash ? SYM_INFO_IN_HASH : 0) | (erw->dsyms.want_gnu_hash ? SYM_INFO_IN_GNU_HASH : 0);
    sym_info_t* sym_info = erw->dsyms.info;
    uint32_t i = count;
    count = 0;
    while (i-- > 1) {
      uint32_t info = sym_info[i].flags;
      if (info & SYM_INFO_LOCAL) continue;
      if (!(info & SYM_INFO_EXPORTABLE)) continue;
      if (!(info & hash_bits)) continue;
      indices[count++] = i;
    }
    erwNNE_(dsyms_sort_indices)(erw, indices, count);
    uint16_t* versym = erw->dsyms.versions;
    struct ElfNN_(Sym)* syms = erw->dsyms.base;
    for (i = 0; i < count; ++i) {
      uint32_t index = indices[i];
      uint32_t info = sym_info[index].flags;
      struct ElfNN_(Sym)* sym = syms + index;
      uint8_t stt = sym->st_info & 0xf;
      printf("%s %s", stt_names[stt], erw_dynstr_decode(erw, Elf_bswapu32(sym->st_name)));
      const char* sep = " (";
      if (versym) {
        uint16_t ver = Elf_bswapu16(versym[index]);
        uint16_t hidden = ver & 0x8000;
        ver &= 0x7fff;
        ver_item_t* item = ver < erw->vers.count ? erw->vers.base[ver].item : NULL;
        if (item) {
          printf("@%s", erw_dynstr_decode(erw, item->str));
          if (hidden) {
            if (ver >= 3) printf("%signored by unversioned lookups", sep), sep = ", ";
            else if (ver == 2) printf("%signored by unversioned dlsym lookups", sep), sep = ", ";
          }
        } else if (hidden) {
          printf("%signored by versioned lookups", sep), sep = ", ";
        }
      }
      if (!(info & SYM_INFO_PLT_EXPORTABLE)) printf("%signored by PLT lookups", sep), sep = ", ";
      if ((stt == STT_OBJECT || stt == STT_COMMON || stt == STT_TLS) && sym->st_size) printf("%s%llu bytes", sep, (long long unsigned)Elf_bswapuNN(sym->st_size)), sep = ", ";
      if (info & (SYM_INFO_RELOC_COPY | SYM_INFO_RELOC_PLT | SYM_INFO_RELOC_REGULAR)) {
        if (info & (SYM_INFO_RELOC_COPY | SYM_INFO_RELOC_REGULAR | SYM_INFO_PLT_EXPORTABLE)) {
          printf("%salso imported", sep), sep = ", "; 
        }
      }
      if ((sym->st_info >> 4) == STB_WEAK) printf("%soptionally weak", sep), sep = ", ";
      if (stt == STT_GNU_IFUNC && (Elf_bswapu16(sym->st_shndx) != SHN_UNDEF)) printf("%sifunc", sep), sep = ", ";
      if (sep[0] != ' ') printf(")");
      printf(" -> 0x%08llx", (long long unsigned)Elf_bswapuNN(sym->st_value));
      printf("\n");
    }
    free(indices);
  }
}

static void erwNNE_(action_print_imports)(erw_state_t* erw) {
  {
    void* itr = erw_phdrs_find_first(erw, PT_INTERP);
    if (itr) {
      printf("interpreter ");
      erwNNE_(print_interpreter)(erw, itr);
    }
  }
  {
    if (!erw->dhdrs.base) erw_dhdrs_init(erw);
    struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
    struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
    struct ElfNN_(Dyn)* rpath = NULL;
    struct ElfNN_(Dyn)* runpath = NULL;
    bool any_libs = false;
    for (; itr != end; ++itr) {
      switch (Elf_bswapuNN(itr->d_tag)) {
      case DT_RPATH: rpath = itr; break;
      case DT_RUNPATH: runpath = itr; break;
      case DT_NEEDED:
      case DT_AUXILIARY:
      case DT_FILTER:
        any_libs = true;
        break;
      }
    }
    if (any_libs) {
      if (runpath) {
        printf("runpath %s\n", erw_dynstr_decode(erw, Elf_bswapuNN(runpath->d_un.d_val)));
      } else if (rpath) {
        printf("rpath %s\n", erw_dynstr_decode(erw, Elf_bswapuNN(rpath->d_un.d_val)));
      }
      erwNNE_(print_imported_libs)(erw);
    }
  }
  {
    if (!erw->vers.count) erw_vers_init(erw);
    for (ver_group_t* g = erw->vers.need; g; g = g->next) {
      for (ver_item_t* i = g->items; i; i = i->next) {
        printf("version  %s from ", erw_dynstr_decode(erw, i->str));
        printf("%s%s\n", erw_dynstr_decode(erw, g->name), (i->flags & VER_FLG_WEAK) ? " (weak)" : "");
      }
    }
  }
  {
    if (!erw->dsyms.base) erw_dsyms_init(erw);
    uint32_t count = erw->dsyms.count;
    uint32_t* indices = (uint32_t*)malloc(count * sizeof(uint32_t));
    sym_info_t* sym_info = erw->dsyms.info;
    uint32_t i = count;
    count = 0;
    while (i-- > 1) {
      uint32_t info = sym_info[i].flags;
      if (info & SYM_INFO_LOCAL) continue;
      if (!(info & (SYM_INFO_RELOC_COPY | SYM_INFO_RELOC_PLT | SYM_INFO_RELOC_REGULAR))) continue;
      indices[count++] = i;
    }
    erwNNE_(dsyms_sort_indices)(erw, indices, count);
    uint16_t* versym = erw->dsyms.versions;
    struct ElfNN_(Sym)* syms = erw->dsyms.base;
    uint16_t hash_bits = (erw->dsyms.want_hash ? SYM_INFO_IN_HASH : 0) | (erw->dsyms.want_gnu_hash ? SYM_INFO_IN_GNU_HASH : 0);
    uint32_t eager_bits = (erw_dhdrs_is_eager(erw) ? SYM_INFO_RELOC_MAYBE_EAGER : 0) | SYM_INFO_RELOC_EAGER;
    for (i = 0; i < count; ++i) {
      uint32_t index = indices[i];
      uint32_t info = sym_info[index].flags;
      struct ElfNN_(Sym)* sym = syms + index;
      uint8_t stt = sym->st_info & 0xf;
      printf("%s %s", stt_names[stt], erw_dynstr_decode(erw, Elf_bswapu32(sym->st_name)));
      const char* sep = " (";
      if (versym) {
        uint16_t ver = Elf_bswapu16(versym[index]) & 0x7fff;
        if (ver < erw->vers.count) {
          ver_index_ref_t* ref = &erw->vers.base[ver];
          ver_item_t* item = ref->item;
          if (item) {
            printf("@%s", erw_dynstr_decode(erw, item->str));
            if (item->index & 0x8000) printf("%swill not bind to VER_NDX_GLOBAL", sep), sep = ", ";
            if (ref->group) {
              printf("%soriginally from %s", sep, erw_dynstr_decode(erw, ref->group->name)), sep = ", ";
            }
          }
        }
      }
      if ((sym->st_info >> 4) == STB_WEAK) printf("%sweak", sep), sep = ", ";
      if (!(info & eager_bits)) printf("%slazy", sep), sep = ", ";
      if (info & SYM_INFO_RELOC_COPY) printf("%scopied", sep), sep = ", ";
      if ((info & SYM_INFO_RELOC_COPY) || ((stt == STT_OBJECT || stt == STT_COMMON || stt == STT_TLS) && sym->st_size)) printf("%s%llu bytes", sep, (long long unsigned)Elf_bswapuNN(sym->st_size)), sep = ", ";
      if (info & SYM_INFO_RELOC_PLT) {
        if (info & (SYM_INFO_RELOC_COPY | SYM_INFO_RELOC_REGULAR)) printf("%sfor both PLT and non-PLT", sep), sep = ", ";
        else printf("%sfor PLT", sep), sep = ", ";
      }
      if ((info & hash_bits) && (info & SYM_INFO_EXPORTABLE)) {
        printf("%salso exported", sep), sep = ", ";
        if (!(info & SYM_INFO_PLT_EXPORTABLE)) {
          printf(" (except to PLTs)");
        }
      }
      if (sep[0] != ' ') printf(")");
      printf("\n");
    }
    free(indices);
  }
}

#if ERW_32
static bool erwE_(action_remove_verneed)(erw_state_t* erw) {
  bool modified = false;
  if (erw->dsyms.versions) {
    // Any symbols sym@X where X comes from a verneed are replaced by
    // just sym, which can bind to unversioned sym, or can bind to
    // sym@Y for certain values of Y (either Y is the oldest version
    // in the library providing sym, or the library providing sym
    // nominates Y as the default version for sym).
    uint32_t nver = erw->vers.count;
    ver_index_ref_t* vers = erw->vers.base;
    uint16_t* versym = erw->dsyms.versions;
    uint32_t nsym = erw->dsyms.count;
    uint32_t i;
    for (i = 0; i < nsym; ++i) {
      uint16_t v = Elf_bswapu16(versym[i]) & 0x7fff;
      if (v > 1 && v < nver && vers[v].group) {
        versym[i] = Elf_bswapu16((erw->dsyms.info[i].flags & SYM_INFO_LOCAL) ? 0 : 1);
        modified = true;
      }
    }
  }
  return modified;
}
#endif

static bool erwNNE_(print_kernel_version)(erw_state_t* erw) {
  struct ElfNN_(Phdr)* itr = (struct ElfNN_(Phdr)*)erw->phdrs.base;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  for (; itr < end; ++itr) {
    if (Elf_bswapu32(itr->p_type) != PT_NOTE) continue;
    Elf_uNN size = Elf_bswapuNN(itr->p_filesz);
    Elf_uNN align = Elf_bswapuNN(itr->p_align);
    if (size < 32 || (align != 4 && align != 8)) continue;
    Elf_uNN offset = Elf_bswapuNN(itr->p_offset);
    if (erw->f_size <= offset) continue;
    if (erw->f_size - offset < size) {
      size = erw->f_size - offset;
      if (size < 32) continue;
    }
    for (;;) {
      uint32_t* contents = (uint32_t*)(erw->f + offset);
      uint32_t vendorsz = Elf_bswapu32(contents[0]);
      uint32_t descsz = Elf_bswapu32(contents[1]);
      uint32_t type = Elf_bswapu32(contents[2]);
      if (vendorsz == 4 && descsz == 16 && type == 1 && memcmp(&contents[3], "GNU", 4) == 0) {
        uint32_t c4 = Elf_bswapu32(contents[4]);
        if (c4) {
          printf("non-Linux kernel (type %u) version %u.%u.%u\n", c4, Elf_bswapu32(contents[5]), Elf_bswapu32(contents[6]), Elf_bswapu32(contents[7]));
        } else {
          printf("Linux %u.%u.%u\n", Elf_bswapu32(contents[5]) & 0xff, Elf_bswapu32(contents[6]) & 0xff, Elf_bswapu32(contents[7]) & 0xff);
        }
        return true;
      }
      Elf_uNN advance = 12 + ((vendorsz + align - 1) &- align) + ((descsz + align - 1) &- align);
      if (advance >= size) break;
      size -= advance;
      if (size < 32) break;
      offset += advance;
    }
  }
  return false;
}

static void erwNNE_(remove_kernel_version)(erw_state_t* erw) {
  struct ElfNN_(Phdr)* itr = (struct ElfNN_(Phdr)*)erw->phdrs.base;
  struct ElfNN_(Phdr)* end = itr + erw->phdrs.count;
  for (; itr < end; ++itr) {
    if (Elf_bswapu32(itr->p_type) != PT_NOTE) continue;
    Elf_uNN size = Elf_bswapuNN(itr->p_filesz);
    Elf_uNN align = Elf_bswapuNN(itr->p_align);
    if (size < 32 || (align != 4 && align != 8)) continue;
    Elf_uNN offset = Elf_bswapuNN(itr->p_offset);
    if (erw->f_size <= offset) continue;
    if (erw->f_size - offset < size) {
      size = erw->f_size - offset;
      if (size < 32) continue;
    }
    for (;;) {
      uint32_t* contents = (uint32_t*)(erw->f + offset);
      uint32_t vendorsz = Elf_bswapu32(contents[0]);
      uint32_t descsz = Elf_bswapu32(contents[1]);
      uint32_t type = Elf_bswapu32(contents[2]);
      if (vendorsz == 4 && descsz == 16 && type == 1 && memcmp(&contents[3], "GNU", 4) == 0) {
        memcpy(&contents[3], "NULL", 4); // Changing vendor name string should cause good tools to skip the note.
        memset(&contents[4], 0, 16);     // Changing the version to 0.0.0 should satisfy any tool that doesn't check vendor name first.
        erw->modified |= ERW_MODIFIED_MISC;
      }
      Elf_uNN advance = 12 + ((vendorsz + align - 1) &- align) + ((descsz + align - 1) &- align);
      if (advance >= size) break;
      size -= advance;
      if (size < 32) break;
      offset += advance;
    }
  }
}
