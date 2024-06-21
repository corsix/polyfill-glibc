#include <string.h>
#include "common.h"
#include "erw.h"

static uint8_t* read_uleb(uint8_t* p, uint64_t* result) {
  uint64_t x = 0;
  uint32_t s = 0;
  for (;;) {
    uint8_t b = *p++;
    if (b & 0x80) {
      x |= (uint64_t)(b & 0x7f) << s;
      s += 7;
    } else {
      x |= (uint64_t)b << s;
      break;
    }
  }
  if (result) *result = x;
  return p;
}

static uint8_t* read_sleb(uint8_t* p, int64_t* result) {
  uint64_t x = 0;
  uint32_t s = 0;
  for (;;) {
    uint8_t b = *p++;
    x |= (uint64_t)(b & 0x7f) << s;
    s += 7;
    if (!(b & 0x80)) break;
  }
  if (result) {
    s = (s < 64) ? 64 - s : 0;
    x <<= s;
    *result = (int64_t)x >> s;
  }
  return p;
}

typedef struct read_ptr_ctx_t {
  uint8_t file_kind;
  uint8_t ptr_encoding;
  erw_state_t* erw;
  uint8_t* ref_p;
  uint64_t ref_v;
  uint64_t textrel;
  uint64_t datarel;
  uint64_t funcrel;
} read_ptr_ctx_t;

static uint8_t* read_ptr(uint8_t* p, uint64_t* result, read_ptr_ctx_t* ctx) {
  uint8_t* p0 = p;
  switch (ctx->ptr_encoding) {
  case 0xff:
    if (result) *result = 0;
    return p;
  case 0x50: {
    uintptr_t align = (ctx->file_kind & ERW_FILE_KIND_64) ? 7 : 3;
    p += ((0u - (uintptr_t)p) & align);
    break; }
  default:
    break;
  }
  switch (ctx->ptr_encoding & 0xF) {
  case 0: if (ctx->file_kind & ERW_FILE_KIND_64) goto case_4; else goto case_3;
  case 1: p = read_uleb(p, result); break;
  case 2: {
    uint16_t u;
    memcpy(&u, p, sizeof(u));
    p += sizeof(u);
    if (ctx->file_kind & ERW_FILE_KIND_BSWAP) u = __builtin_bswap16(u);
    if (result) *result = u;
    break; }
  case 3: case_3: {
    uint32_t u;
    memcpy(&u, p, sizeof(u));
    p += sizeof(u);
    if (ctx->file_kind & ERW_FILE_KIND_BSWAP) u = __builtin_bswap32(u);
    if (result) *result = u;
    break; }
  case 4: case_4: {
    uint64_t u;
    memcpy(&u, p, sizeof(u));
    p += sizeof(u);
    if (ctx->file_kind & ERW_FILE_KIND_BSWAP) u = __builtin_bswap64(u);
    if (result) *result = u;
    break; }
  case 9: {
    int64_t s;
    p = read_sleb(p, &s);
    if (result) *result = (uint64_t)s;
    break; }
  case 10: {
    int16_t s;
    memcpy(&s, p, sizeof(s));
    p += sizeof(s);
    if (ctx->file_kind & ERW_FILE_KIND_BSWAP) s = __builtin_bswap16(s);
    if (result) *result = (uint64_t)(int64_t)s;
    break; }
  case 11: {
    int32_t s;
    memcpy(&s, p, sizeof(s));
    p += sizeof(s);
    if (ctx->file_kind & ERW_FILE_KIND_BSWAP) s = __builtin_bswap32(s);
    if (result) *result = (uint64_t)(int64_t)s;
    break; }
  case 12: {
    int64_t s;
    memcpy(&s, p, sizeof(s));
    p += sizeof(s);
    if (ctx->file_kind & ERW_FILE_KIND_BSWAP) s = __builtin_bswap64(s);
    if (result) *result = (uint64_t)s;
    break; }
  default:
    if (result) *result = 0;
    break;
  }
  if (result && *result) {
    switch (ctx->ptr_encoding >> 4) {
    case 1: *result += ctx->ref_v + (p0 - ctx->ref_p); break;
    case 2: *result += ctx->textrel; break;
    case 3: *result += ctx->datarel; break;
    case 4: *result += ctx->funcrel; break;
    }
    if (!(ctx->file_kind & ERW_FILE_KIND_64)) {
      *result = (uint32_t)*result;
    }
    if (ctx->ptr_encoding & 0x80) {
      FATAL("TODO");
    }
  }
  return p;
}

static void print_dw_bytecode(erw_state_t* erw, uint8_t* bc, uint8_t* end, read_ptr_ctx_t* ctx) {
  (void)ctx;
  while (bc < end) {
    uint8_t b = *bc++;
    if (b < 0x40) {
      switch (b) {
      case 0x00:
        printf("  DW_CFA_nop\n");
        break;
      case 0x02:
        printf("  DW_CFA_advance_loc1(%u * code_align)\n", *bc++);
        break;
      case 0x03: {
        uint16_t advance;
        memcpy(&advance, bc, sizeof(advance));
        bc += sizeof(advance);
        if (erw->file_kind & ERW_FILE_KIND_BSWAP) advance = __builtin_bswap16(advance);
        printf("  DW_CFA_advance_loc2(%u * code_align)\n", advance);
        break; }
      case 0x07: {
        uint64_t reg;
        bc = read_uleb(bc, &reg);
        printf("  DW_CFA_undefined(r%llu)\n", (unsigned long long)reg);
        break; }
      case 0x0a:
        printf("  DW_CFA_remember_state\n");
        break;
      case 0x0b:
        printf("  DW_CFA_restore_state\n");
        break;
      case 0x0c: {
        uint64_t reg, off;
        bc = read_uleb(read_uleb(bc, &reg), &off);
        printf("  DW_CFA_def_cfa(r%llu, +%llu)\n", (unsigned long long)reg, (unsigned long long)off);
        break; }
      case 0x0d: {
        uint64_t reg;
        bc = read_uleb(bc, &reg);
        printf("  DW_CFA_def_cfa_register(r%llu)\n", (unsigned long long)reg);
        break; }
      case 0x0e: {
        uint64_t off;
        bc = read_uleb(bc, &off);
        printf("  DW_CFA_def_cfa_offset(+%llu)\n", (unsigned long long)off);
        break; }
      case 0x2d:
        printf("  DW_CFA_GNU_window_save\n");
        break;
      default:
        printf("  Unknown DW_CFA_ bytecode 0x%02x\n", b);
        break;
      }
    } else if (b < 0x80) {
      printf("  DW_CFA_advance_loc(%u * code_align)\n", b & 0x3f);
    } else if (b < 0xC0) {
      uint64_t off;
      bc = read_uleb(bc, &off);
      printf("  DW_CFA_offset(r%u, CFA + %llu * data_align)\n", b & 0x3f, (unsigned long long)off);
    } else {
      printf("  DW_CFA_restore(r%u)\n", b & 0x3f);
    }
  }
}

static void print_fde(erw_state_t* erw, uint64_t vaddr, read_ptr_ctx_t* ctx) {
  uint8_t* fde = erw_view_v(erw, vaddr);
  if (!fde) {
    printf("  Could not read FDE at 0x%llx\n", (unsigned long long)vaddr);
    return;
  }
  uint32_t fde_length;
  int32_t cie_off;
  memcpy(&fde_length, fde, 4);
  memcpy(&cie_off, fde + 4, 4);
  fde += 8;
  if (erw->file_kind & ERW_FILE_KIND_BSWAP) {
    fde_length = __builtin_bswap32(fde_length);
    cie_off = __builtin_bswap32(cie_off);
  }
  uint8_t* fde_end = fde + fde_length - 4;
  uint64_t cie_addr = vaddr + 4 - (int64_t)cie_off;
  if (!(erw->file_kind & ERW_FILE_KIND_64)) {
    cie_addr = (uint32_t)cie_addr;
  }
  printf("  CIE at 0x%llx:\n", (unsigned long long)cie_addr);
  uint8_t* cie = erw_view_v(erw, cie_addr);
  ctx->ref_v = cie_addr;
  ctx->ref_p = cie;
  uint32_t cie_length;
  memcpy(&cie_length, cie, 4);
  memcpy(&cie_off, cie + 4, 4);
  cie += 8;
  if (erw->file_kind & ERW_FILE_KIND_BSWAP) {
    cie_length = __builtin_bswap32(cie_length);
    cie_off = __builtin_bswap32(cie_off);
  }
  uint8_t* cie_end = cie + cie_length - 4;
  if (cie_off != 0) {
    printf("    Zero field has non-zero value %d\n", cie_off);
  }
  uint8_t cie_version = *cie++;
  const char* cie_aug_str = (char*)cie;
  printf("    Version %u, aug_string=%s\n", cie_version, cie_aug_str);
  do {} while (*cie++);
  if (cie_aug_str[0] == 'e' && cie_aug_str[1] == 'h') {
    cie += (erw->file_kind & ERW_FILE_KIND_64) ? 8 : 4;
  }
  if (cie_version >= 4) {
    printf("    Address size %u, segment size %u\n", cie[0], cie[1]);
    cie += 2;
  }
  uint64_t code_align, ret_addr_ord;
  int64_t data_align;
  cie = read_sleb(read_uleb(cie, &code_align), &data_align);
  if (cie_version <= 1) {
    ret_addr_ord = *cie++;
  } else {
    cie = read_uleb(cie, &ret_addr_ord);
  }
  printf("    Code align %llu, data align %lld, return address in r%llu\n", (unsigned long long)code_align, (long long)data_align, (unsigned long long)ret_addr_ord);
  uint8_t* cie_dw = NULL;
  uint8_t fde_ptr_encoding = 0;
  uint8_t lsda_ptr_encoding = 0;
  const char* itr = cie_aug_str;
  if (itr[0] == 'e' && itr[1] == 'h') itr += 2;
  for (;;) {
    char c;
    switch ((c = *itr++)) {
    case 'z': {
      uint64_t aug_len;
      cie = read_uleb(cie, &aug_len);
      cie_dw = cie + aug_len;
      continue; }
    case 'R':
      fde_ptr_encoding = *cie++;
      continue;
    case 'P': {
      uint64_t p_ptr;
      ctx->ptr_encoding = *cie++;
      cie = read_ptr(cie, &p_ptr, ctx);
      printf("    Personality routine at 0x%llx\n", (unsigned long long)p_ptr);
      continue; }
    case 'L': 
      lsda_ptr_encoding = *cie++;
      continue;
    case 'S':
      continue;
    case 'B':
      continue;
    case '\0':
      cie_dw = cie;
      break;
    default:
      printf("    Unknown augmentation character %c\n", c);
      break;
    }
    break;
  }
  if (cie_dw && cie_dw < cie_end) {
    print_dw_bytecode(erw, cie_dw, cie_end, ctx);
  }
  ctx->ref_v = vaddr + 8;
  ctx->ref_p = fde;
  ctx->ptr_encoding = fde_ptr_encoding;
  uint64_t func_start, func_length;
  fde = read_ptr(fde, &func_start, ctx);
  ctx->ptr_encoding = fde_ptr_encoding & 0xF;
  fde = read_ptr(fde, &func_length, ctx);
  printf("  Function starting at 0x%llx, ending at 0x%llx\n", (unsigned long long)func_start, (unsigned long long)(func_start + func_length));
  uint8_t* fde_dw = NULL;
  itr = cie_aug_str;
  if (itr[0] == 'e' && itr[1] == 'h') itr += 2;
  for (;;) {
    char c;
    switch ((c = *itr++)) {
    case 'z': {
      uint64_t aug_len;
      fde = read_uleb(fde, &aug_len);
      fde_dw = fde + aug_len;
      continue; }
    case 'R': case 'P': case 'S': case 'B': continue;
    case 'L': {
      uint64_t l_ptr;
      ctx->ptr_encoding = lsda_ptr_encoding;
      fde = read_ptr(fde, &l_ptr, ctx);
      printf("    LSDA pointer 0x%llx\n", (unsigned long long)l_ptr);
      continue; }
    case '\0':
      fde_dw = fde;
      break;
    default:
      printf("    Unknown augmentation character %c\n", c);
      break;
    }
    break;
  }
  if (fde_dw && fde_dw < fde_end) {
    print_dw_bytecode(erw, fde_dw, fde_end, ctx);
  }
}

typedef struct eh_frame_hdr_t {
  uint8_t version;
  uint8_t eh_frame_ptr_enc;
  uint8_t fde_count_enc;
  uint8_t table_enc;
  uint32_t eh_frame_ptr;
  uint32_t fde_count;
  struct {
    int32_t func_start;
    int32_t fde_offset;
  } sorted_table[];
} eh_frame_hdr_t;

void print_eh_frame_hdr_at(erw_state_t* erw, uint64_t vaddr) {
  eh_frame_hdr_t* hdr = (eh_frame_hdr_t*)erw_view_v(erw, vaddr);
  if (!hdr) {
    printf("Could not read PT_GNU_EH_FRAME at 0x%llx\n", (unsigned long long)vaddr);
    return;
  }
  if (hdr->version != 1 || hdr->eh_frame_ptr_enc != 0x1b || hdr->fde_count_enc != 0x03 || hdr->table_enc != 0x3b) {
    printf("Bad PT_GNU_EH_FRAME header: %02x %02x %02x %02x\n", hdr->version, hdr->eh_frame_ptr_enc, hdr->fde_count_enc, hdr->table_enc);
    return;
  }
  uint32_t fde_count = hdr->fde_count;
  if (erw->file_kind & ERW_FILE_KIND_BSWAP) fde_count = __builtin_bswap32(fde_count);
  printf("%u FDEs:\n", fde_count);
  read_ptr_ctx_t ctx;
  ctx.file_kind = erw->file_kind;
  ctx.erw = erw;
  ctx.textrel = 0;
  ctx.datarel = 0;
  for (uint32_t i = 0; i < fde_count; ++i) {
    int32_t func_start = hdr->sorted_table[i].func_start;
    int32_t fde_offset = hdr->sorted_table[i].fde_offset;
    if (erw->file_kind & ERW_FILE_KIND_BSWAP) {
      func_start = __builtin_bswap32(func_start);
      fde_offset = __builtin_bswap32(fde_offset);
    }
    uint64_t func_addr = vaddr + (int64_t)func_start;
    uint64_t fde_addr = vaddr + (int64_t)fde_offset;
    if (!(erw->file_kind & ERW_FILE_KIND_64)) {
      func_addr = (uint32_t)func_addr;
      fde_addr = (uint32_t)fde_addr;
    }
    ctx.funcrel = func_addr;
    printf("Function starting at 0x%llx has FDE at 0x%llx:\n", (unsigned long long)func_addr, (unsigned long long)fde_addr);
    print_fde(erw, fde_addr, &ctx);
  }
}
