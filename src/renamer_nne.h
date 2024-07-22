static void erwNNE_(renamer_apply_to)(erw_state_t* erw, renamer_t* self) {
  uint16_t ver_cache = 0;
  do {
    self->added_renames = false;
    if (self->options->create_polyfill_so) {
      ver_cache = 0;
      renamer_create_polyfill_so(self, &ver_cache);
    }
    struct ElfNN_(Sym)* syms = erw->dsyms.base;
    uint16_t* symvers = erw->dsyms.versions;
    sym_info_t* infos = erw->dsyms.info;
    uint16_t hash_bits = (erw->dsyms.want_hash ? SYM_INFO_IN_HASH : 0) | (erw->dsyms.want_gnu_hash ? SYM_INFO_IN_GNU_HASH : 0);
    for (uint32_t i = 0, nsym = erw->dsyms.count; i < nsym; ++i) {
      if (infos[i].flags & (SYM_INFO_LOCAL | SYM_INFO_WANT_POLYFILL)) continue;
      struct ElfNN_(Sym)* sym = syms + i;
      const char* name = erw_dynstr_decode(erw, Elf_bswapu32(sym->st_name));
      const char* vstr = NULL;
      ver_group_t* vref_group = NULL;
      ver_item_t* vref_item = NULL;
      if (symvers) {
        ver_index_ref_t* vref = erw->vers.base + (Elf_bswapu16(symvers[i]) & 0x7fff);
        vref_group = vref->group;
        if ((vref_item = vref->item)) {
          vstr = erw_dynstr_decode(erw, vref_item->str);
        }
      }
      renamed_symbol_t renamed;
      renamed.lib = NULL;
      renamed.name = name;
      renamed.version = vstr;
      if (!renamer_apply_to_name(self, &renamed)) continue;
      if (renamed.lib) {
        uint32_t flags = infos[i].flags;
        if ((flags & hash_bits) && (flags & SYM_INFO_EXPORTABLE)) {
          // Symbol is exported.
          if (!(flags & (SYM_INFO_RELOC_EAGER | SYM_INFO_RELOC_MAYBE_EAGER))) {
            // Not imported. It doesn't make sense to rename an export to a
            // different library, so skip the rename.
            continue;
          } else if ((flags & SYM_INFO_PLT_EXPORTABLE) || !(flags & SYM_INFO_RELOC_PLT)) {
            // Both imported and exported. The import allows for interposition,
            // but will be satisfied by the export if not interposed. The
            // export might also be used by other libraries. It doesn't make
            // sense to rename an export to a different library, and the import
            // is tied to the export, so skip the rename.
            continue;
          } else {
            // Both imported and exported, but the export merely provides a
            // canonical address for the symbol, and the import cannot be
            // satisfied by the export. Proceed with the rename.
          }
        }
        if (strcmp(renamed.lib, "polyfill") == 0) {
          if (renamed.version == NULL) {
            renamed.version = "POLYFILL";
          }
          ensure_has_polyfiller(self);
          uint32_t token = (self->polyfiller_fns->add)(self->polyfiller, renamed.name, NULL);
          if (self->options->use_polyfill_so) {
            // Done after adding the name to the polyfiller, in case adding it
            // imposes additional renames.
            renamed.lib = self->options->use_polyfill_so;
            goto use_polyfill_so;
          }
          if (!token) {
            FATAL("Missing implementation for polyfill::%s (required by %s)", renamed.name, name);
          }
          syms[i].st_value = Elf_bswapuNN(token);
          erw->modified |= ERW_MODIFIED_DSYMS;
          if ((flags & hash_bits) && (flags & SYM_INFO_EXPORTABLE)) {
            infos[i].flags |= SYM_INFO_WANT_POLYFILL | SYM_INFO_PLT_EXPORTABLE;
            if (syms[i].st_shndx == SHN_UNDEF) {
              syms[i].st_shndx = 0xffff;
            }
          } else {
            infos[i].flags |= SYM_INFO_WANT_POLYFILL | SYM_INFO_LOCAL;
            // We want to make the symbol local, but we don't want to have to
            // rewrite the relocations against it. This requires changing its
            // binding to STB_LOCAL, and its visibility to STV_HIDDEN.
            // Changing the visibility is required for lazy relocations, as
            // glibc's logic there checks visibility but not binding.
            // Changing the binding is required for eager relocations, as prior
            // to version 2.24 (in particular commit b6084a958f), glibc's logic
            // there checks binding but not visibility.
            syms[i].st_info = (syms[i].st_info & 0xf) | (STB_LOCAL << 4);
            syms[i].st_other = (syms[i].st_other & ~3) | STV_HIDDEN;
            // Remove the version suffix from the symbol, as it is no longer
            // required.
            if (symvers) {
              symvers[i] = 0;
              erw->modified |= vref_group ? ERW_MODIFIED_VERNEED : ERW_MODIFIED_VERDEF;
              if (vref_item) vref_item->state |= VER_EDIT_STATE_REMOVED_REFS;
            }
            continue; // Symbol has been made local; no need to actually rename it.
          }
        } else { use_polyfill_so:
          renamer_ensure_lib_dependency(self, renamed.lib);
        }
      }
      if (strcmp(name, renamed.name) != 0) {
        uint32_t modified = ERW_MODIFIED_DSYMS;
        if (infos[i].flags & (SYM_INFO_IN_HASH | SYM_INFO_IN_GNU_HASH)) {
          modified |= ERW_MODIFIED_HASH_TABLES;
        }
        erw->modified |= modified;
        syms[i].st_name = Elf_bswapu32(erw_dynstr_encode(erw, renamed.name));
      }
      if ((renamed.lib && renamed.version) || !xstreq(vstr, renamed.version)) {
        if (renamed.version) {
          if (!symvers) {
            erw_dsyms_ensure_versions_array(erw);
            symvers = erw->dsyms.versions;
          }
          if (!renamed.lib && vref_group) {
            renamed.lib = erw_dynstr_decode(erw, vref_group->name);
          }
          if (infos[i].flags & SYM_INFO_WANT_POLYFILL) {
            renamed.lib = NULL;
          }
          uint16_t new_ver = erw_vers_find_or_add(erw, &ver_cache, renamed.lib, renamed.version);
          if ((new_ver ^ Elf_bswapu16(symvers[i])) & 0x7fff) {
            erw->modified |= vref_group ? ERW_MODIFIED_VERNEED : ERW_MODIFIED_VERDEF;
            if (vref_item) vref_item->state |= VER_EDIT_STATE_REMOVED_REFS;
            if (strcmp(renamed.version, "POLYFILL") == 0) {
              // Really want to bind to the POLYFILL version, and never to
              // a unversioned symbol with the same base name.
              new_ver |= 0x8000;
            }
            symvers[i] = Elf_bswapu16(new_ver);
          }
        } else if (symvers && symvers[i] != Elf_bswapu16(1)) {
          erw->modified |= vref_group ? ERW_MODIFIED_VERNEED : ERW_MODIFIED_VERDEF;
          if (vref_item) vref_item->state |= VER_EDIT_STATE_REMOVED_REFS;
          symvers[i] = Elf_bswapu16(1);
        }
      }
    }
    // Note that sometimes adding a polyfill can impose a rename, in which case
    // there'll be some new renames, and we go round again.
  } while (self->added_renames);

  // Check that we can meet the target version now, as doing so can request a
  // DT_RELR fixup from the polyfiller.
  renamer_confirm_target_version(self);

  // Do all the polyfills
  if (self->polyfiller_fns && !self->options->use_polyfill_so) {
    void* polyfiller = self->polyfiller;
    (self->polyfiller_fns->finished_adding)(polyfiller);
    struct ElfNN_(Sym)* syms = erw->dsyms.base;
    sym_info_t* infos = erw->dsyms.info;
    uint64_t (*addr_of)(void*, uint32_t) = self->polyfiller_fns->addr_of;
    for (uint32_t i = 0, nsym = erw->dsyms.count; i < nsym; ++i) {
      if (infos[i].flags & SYM_INFO_WANT_POLYFILL) {
        infos[i].flags -= SYM_INFO_WANT_POLYFILL;
        syms[i].st_value = Elf_bswapuNN(addr_of(polyfiller, Elf_bswapuNN(syms[i].st_value)));
      }
    }
  }

  // Check target version again, in case polyfills added new externs.
  renamer_confirm_target_version(self);

  // Handle lib dependencies
  if (self->ensured_libs.count) {
    struct ElfNN_(Dyn)* itr = erw->dhdrs.base;
    struct ElfNN_(Dyn)* end = itr + erw->dhdrs.count;
    for (; itr < end; ++itr) {
      switch (Elf_bswapuNN(itr->d_tag)) {
      case DT_NEEDED:
      case DT_FILTER: {
        const char* lname = erw_dynstr_decode(erw, Elf_bswapuNN(itr->d_un.d_val));
        unsigned char* p = sht_lookup_p(&self->ensured_libs, lname, strlen(lname));
        if (p) {
          *p = 0;
        }
        break; }
      }
    }
    for (unsigned char* p = sht_iter_start_p(&self->ensured_libs); p; p = sht_iter_next_p(&self->ensured_libs, p)) {
      if (*p) {
        erw_dhdrs_add_late_needed(erw, sht_p_key(p));
      }
    }
    sht_clear(&self->ensured_libs);
  }
}

#ifdef ERW_32
static void erwE_(collect_bad_version_syms)(erw_state_t* erw, bool* ver_bad, u32_array_t* bad_syms) {
  uint16_t* versym = erw->dsyms.versions;
  uint32_t nsym = erw->dsyms.count;
  uint32_t nver = erw->vers.count;
  for (uint32_t i = 0; i < nsym; ++i) {
    uint16_t v = Elf_bswapu16(versym[i]) & 0x7fff;
    if (v < nver && ver_bad[v]) {
      if (bad_syms->count >= bad_syms->capacity) {
        u32_array_increase_capacity(bad_syms);
      }
      bad_syms->base[bad_syms->count++] = i;
    }
  }
}
#endif

static void erwNNE_(print_bad_syms)(erw_state_t* erw, uint32_t* indices, uint32_t count) {
  erwNNE_(dsyms_sort_indices)(erw, indices, count);
  uint16_t* versym = erw->dsyms.versions;
  struct ElfNN_(Sym)* syms = erw->dsyms.base;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t index = indices[i];
    struct ElfNN_(Sym)* sym = syms + index;
    const char* vstr = NULL;
    if (versym) {
      uint16_t ver = Elf_bswapu16(versym[index]) & 0x7fff;
      if (ver < erw->vers.count) {
        ver_index_ref_t* ref = &erw->vers.base[ver];
        ver_item_t* item = ref->item;
        if (item) {
          vstr = erw_dynstr_decode(erw, item->str);
        }
      }
    }
    fprintf(stderr, "  %s%s%s\n", erw_dynstr_decode(erw, Elf_bswapu32(sym->st_name)), vstr ? "@" : "", vstr);
  }
}
