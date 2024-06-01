static void erwNNE_(set_relative_interp)(mini_erw_state_t* erw, const char* interp_path) {
  // Map in the program headers (unless already mapped in).
  struct ElfNN_(Ehdr)* hdr = erw->header;
  struct ElfNN_(Phdr)* ph;
  struct ElfNN_(Phdr)* phend;
  {
    Elf_uNN off = Elf_bswapuNN(hdr->e_phoff);
    Elf_uNN size = Elf_bswapu16(hdr->e_phnum) * sizeof(struct ElfNN_(Phdr));
    if (off + size <= 4096) {
      ph = (struct ElfNN_(Phdr)*)((char*)hdr + off);
    } else {
      Elf_uNN align = off & 4095;
      void* map = mmap(NULL, size + align, PROT_READ | PROT_WRITE, MAP_SHARED, erw->fd, off - align);
      if (map == MAP_FAILED) {
        FATAL("Could not mmap file %s", erw->filename);
      }
      ph = (struct ElfNN_(Phdr)*)((char*)map + align);
    }
    phend = (struct ElfNN_(Phdr)*)((char*)ph + size);
  }

  // One pass through the program headers; filter out PT_INTERP and
  // identify maximum vaddr and alignment of PT_LOAD.
  struct ElfNN_(Phdr)* out = ph;
  Elf_uNN max_v = 0;
  Elf_uNN max_a = 4096;
  for (struct ElfNN_(Phdr)* itr = ph; itr != phend; ++itr) {
    switch (Elf_bswapu32(itr->p_type)) {
    case PT_INTERP:
      continue;
    case PT_LOAD: {
      Elf_uNN v = Elf_bswapuNN(itr->p_vaddr) + Elf_bswapuNN(itr->p_memsz);
      if (v > max_v) max_v = v;
      Elf_uNN a = Elf_bswapuNN(itr->p_align);
      if (a > max_a) max_a = a;
      break; }
    }
    if (itr != out) memcpy(out, itr, sizeof(*itr));
    ++out;
  }
  if (out == phend) FATAL("%s does not contain PT_INTERP", erw->filename);
  max_v = (max_v + max_a - 1) & -max_a;

  // Determine where new data will go.
  uint64_t new_offset = (erw->f_size + 15) &~ 15ull;
  uint64_t new_v = max_v | (new_offset & (max_a - 1));

  // Write out new PT_LOAD. Note that there is always space for
  // one new PT_LOAD, due to removing at least one PT_INTERP.
  out->p_type = Elf_bswapu32(PT_LOAD);
  out->p_flags = Elf_bswapu32(PF_R | PF_X);
  out->p_offset = Elf_bswapuNN(new_offset);
  out->p_paddr = out->p_vaddr = Elf_bswapuNN(new_v);
  out->p_align = Elf_bswapuNN(max_a);
  // NB: p_filesz and p_memsz filled in later.

  // Patch e_entry.
  Elf_uNN old_entry = Elf_bswapuNN(hdr->e_entry);
  hdr->e_entry = Elf_bswapuNN(new_v);

  // Construct iovec array containing data to append to file.
  struct iovec iovs[4];
  struct iovec* iov = iovs;

  iov->iov_base = (void*)"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
  iov->iov_len = new_offset - erw->f_size;
  ++iov;

  iov->iov_base = (void*)relative_interp_payload;
  new_v += (iov->iov_len = sizeof(relative_interp_payload));
  ++iov;

  uint64_t our_data[2];
  our_data[0] = old_entry - new_v;
  size_t interp_path_len = strlen(interp_path) + 1;
  *(uint16_t*)(our_data + 1) = interp_path_len;
  iov->iov_base = our_data;
  new_v += (iov->iov_len = sizeof(uint64_t) + sizeof(uint16_t));
  ++iov;

  iov->iov_base = (void*)interp_path;
  new_v += (iov->iov_len = interp_path_len);
  ++iov;

  // Finalise the new PT_LOAD.
  out->p_filesz = out->p_memsz = new_v - Elf_bswapuNN(out->p_vaddr);
  ++out;
  memset(out, 0, (char*)phend - (char*)out);

  // Append new data to file.
  writev_all(erw, iovs, iov - iovs);
}
