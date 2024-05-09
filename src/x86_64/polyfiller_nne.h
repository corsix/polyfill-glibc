static void erwNNE_(find_existing_relocs)(erw_state_t* erw, uuht_t* dsym_to_ext, local_info_t* syms) {
  reloc_editor_t* editor = erw->relocs;
  for (; editor < erw->relocs + MAX_RELOC_EDITORS; ++editor) {
    if (!editor->base) break;
    if (editor->dt != DT_RELA && editor->dt != DT_JMPREL) continue;
    if (!(editor->flags & RELOC_EDIT_FLAG_EXPLICIT_ADDEND)) continue;
    uint32_t step = editor->entry_size;
    if (step < sizeof(struct ElfNN_(Rela))) continue;
    struct ElfNN_(Rela)* r = (struct ElfNN_(Rela)*)editor->base;
    for (uint32_t count = editor->count; count; --count, r = (struct ElfNN_(Rela)*)((char*)r + step)) {
      if (r->r_addend) continue;
      uint32_t sidx, rkind;
      Elf_uNN info = Elf_bswapuNN(r->r_info);
#ifdef ERW_32
      rkind = (uint8_t)info;
      sidx = info >> 8;
#else
      rkind = (uint32_t)info;
      sidx = info >> 32;
#endif
      if (rkind != R_X86_64_GLOB_DAT && rkind != R_X86_64_JUMP_SLOT) continue;
      uint64_t k = uuht_contains(dsym_to_ext, sidx);
      if (!k) continue;
      syms[k >> 32].addr = Elf_bswapuNN(r->r_offset);
    }
  }
}
