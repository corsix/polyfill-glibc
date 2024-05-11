#include <stdbool.h>
#include <string.h>
#include "assembler.h"
#include "../uuht.h"
#include "../tokenise.h"
#include "../common.h"
#include "../erw.h"
#include "../../build/x86_64/assembler_gen.h"

#define KWS(KW) \
  KW("\003", cfi) \
  KW("\004", byte) \
  KW("\005", qword) \
  KW("\006", asciiz) \
  KW("\005", align) \
  KW("\004", init) \
  KW("\004", fini) \
  KW("\006", public) \
  KW("\010", function) \
  KW("\005", const) \
  KW("\010", variable) \
  KW("\006", extern) \
  KW("\010", linkname) \
  KW("\015", impose_rename) \
  KW("\014", u8_label_lut) \
  KW("\013", phantom_ref) \
  KW("\010", cfi_byte) \
  KW("\022", cfi_remember_state) \
  KW("\021", cfi_restore_state)
#include "../sht_keywords.h"

typedef struct asm_sym_t {
  uint32_t name; // sht index
  uint32_t flags; // ASM_SYM_FLAG_*
  uint32_t payload_start;
  uint32_t payload_end;
} asm_sym_t;

typedef struct cfi_state_t {
  uint8_t cfa_reg;
  uint8_t n_push;
  uint16_t saved_mask;
} cfi_state_t;

typedef struct parse_ctx_t {
  tokeniser_t toks;
  uuht_t sym_names; // key is toks.sht index, value is index into syms
  asm_sym_t* syms;
  uint32_t syms_size;
  uint32_t syms_capacity;
  uint8_t* payloads;
  uint32_t payloads_size;
  uint32_t payloads_capacity;
  cfi_state_t cfi;
  cfi_state_t pushed_cfi;
} parse_ctx_t;

static uint8_t* ensure_payloads_space(parse_ctx_t* ctx, uint32_t space) {
  uint32_t size = ctx->payloads_size;
  uint32_t capacity = ctx->payloads_capacity;
  uint8_t* payloads = ctx->payloads;
  if ((capacity - size) < space) {
    capacity += size < capacity ? capacity : size;
    ctx->payloads_capacity = capacity;
    ctx->payloads = payloads = realloc(payloads, capacity);
  }
  return payloads + size;
}

typedef struct parsed_operands_t {
  uint8_t op[7]; // Kind in high three bits, register index or other payload in low five.
  uint8_t n;
  uint8_t inferred_size; // Size in top three bits, next bit set if size was inferred, next bit set if another pass required. Low three bits immediate size class.
  uint8_t modrm_base; // As per op, or OP_SPECIAL_NOTHING.
  uint8_t modrm_index; // As per op, or OP_SPECIAL_NOTHING.
  uint8_t modrm_etc; // Memory size in top three bits (7 means X87), next bit set if size known. Next two bits unused. Scale factor for index in low two bits.
  uint64_t modrm_imm;
  uint64_t imm;
} parsed_operands_t;

#define INFERRED_SIZE_KNOWN 0x10
#define INFERRED_SIZE_NEED_RETRY 0x08
#define INFERRED_SIZE_IMM_SIZE_MASK 0x07

#define MODRM_ETC_SIZE_KNOWN 0x10
#define MODRM_ETC_SCALE_MASK 0x03

static void parse_err(token_t* where, const char* what, parse_ctx_t* ctx) {
  tokeniser_error(&ctx->toks, where, what);
}

static void force_decode_uint(token_t* src, uint64_t* dst, parse_ctx_t* ctx) {
  if (!tokeniser_decode_uint(&ctx->toks, src, dst)) {
    parse_err(src, "invalid number", ctx);
  }
}

static token_t* parse_imm(token_t* src, uint64_t* dst, parse_ctx_t* ctx) {
  uint64_t sign = 0;
  for (;;) {
    uint32_t t = (src++)->type;
    if (t == TOK_PUNCT('+')) {
    } else if (t == TOK_PUNCT('-')) {
      sign = ~sign;
    } else if (t == TOK_NUMBER) {
      force_decode_uint(src - 1, dst, ctx);
      break;
    } else if (t == TOK_CHAR_LIT || t == TOK_CHAR_LIT_ESC) {
      *dst = tokeniser_decode_char_lit(&ctx->toks, src - 1);
      break;
    } else {
      parse_err(src - 1, "expected number", ctx);
    }
  }
  *dst = (*dst ^ sign) - sign;
  return src;
}

static uint32_t parse_reg_in_mem_term(token_t* src, parse_ctx_t* ctx) {
  uint32_t tt = src->type;
  if (tt < TOK_IDENT_THR) {
    uint32_t info = *(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt) & 0xff;
    if (info) {
      info ^= OP_SPECIAL_NOTHING;
      if (PACK_OP(1, 0) <= info && info < PACK_OP(7, 0)) {
        return info;
      }
    }
  }
  return 0;
}

static token_t* parse_mem_term(token_t* src, parsed_operands_t* dst, parse_ctx_t* ctx) {
  uint32_t tt = src->type;
  uint32_t reg;
  uint64_t scale = 1;
  if (tt == TOK_NUMBER && src[1].type == TOK_PUNCT('*') && (reg = parse_reg_in_mem_term(src + 2, ctx))) {
    force_decode_uint(src, &scale, ctx);
    src += 3;
  } else if ((reg = parse_reg_in_mem_term(src, ctx))) {
    src += 1;
    if (src->type == TOK_PUNCT('*') && src[1].type == TOK_NUMBER) {
      force_decode_uint(src + 1, &scale, ctx);
      src += 2;
    }
  } else if (tt == TOK_PUNCT('-') || tt == TOK_NUMBER || tt == TOK_CHAR_LIT || tt == TOK_CHAR_LIT_ESC) {
    src = parse_imm(src, &scale, ctx);
    dst->modrm_imm += scale;
    return src;
  } else {
    parse_err(src, "invalid memory operand", ctx);
  }
  if (scale != 0) {
    if (scale == 1 && dst->modrm_base == OP_SPECIAL_NOTHING && reg < PACK_OP(4, 0)) {
      dst->modrm_base = reg;
    } else if (dst->modrm_index == OP_SPECIAL_NOTHING) {
      if ((scale & 1) && (scale >= 3) && dst->modrm_base == OP_SPECIAL_NOTHING) {
        dst->modrm_base = reg;
        --scale;
      }
      if (scale == 1) scale = 0;
      else if (scale == 2) scale = 1;
      else if (scale == 4) scale = 2;
      else if (scale == 8) scale = 3;
      else parse_err(src - 1, "bad scale factor in memory operand", ctx);
      dst->modrm_index = reg;
      dst->modrm_etc |= scale;
    } else {
      parse_err(src - 1, "too many scaled terms in memory operand", ctx);
    }
  }
  return src;
}

static void normalise_mem(parsed_operands_t* dst) {
  // [reg*2] -> [reg+reg] (shorter encoding)
  if (dst->modrm_base == OP_SPECIAL_NOTHING && dst->modrm_index < PACK_OP(4, 0) && dst->modrm_imm == 0 && (dst->modrm_etc & MODRM_ETC_SCALE_MASK) == 1) {
    dst->modrm_base = dst->modrm_index;
    dst->modrm_etc -= 1;
  }
}

static token_t* parse_mem(token_t* src, parsed_operands_t* dst, parse_ctx_t* ctx) {
  dst->modrm_base = OP_SPECIAL_NOTHING;
  dst->modrm_index = OP_SPECIAL_NOTHING;
  dst->modrm_imm = 0;

  uint32_t tt = src->type;
  if (tt == TOK_PUNCT('&')) {
    tt = src[1].type;
    if (tt >= TOK_IDENT_THR) {
      parse_err(src, "expected symbol name after &", ctx);
    }
    dst->modrm_base = OP_SPECIAL_SYMBOL;
    dst->modrm_imm = tt;
    src += 2;
    if (src->type != TOK_PUNCT(']')) {
      parse_err(src, "expected ] after symbol name", ctx);
    }
    return src + 1;
  } else {
    while (tt == TOK_PUNCT('+')) {
      tt = (++src)->type;
    }
    for (;;) {
      src = parse_mem_term(src, dst, ctx);
      tt = src->type;
      if (tt == TOK_PUNCT(']')) {
        normalise_mem(dst);
        return src + 1;
      } else if (tt == TOK_PUNCT('+')) {
        do {
          tt = (++src)->type;
        } while (tt == TOK_PUNCT('+'));
      } else if (tt == TOK_PUNCT('-')) {
        // consume the - as part of the term
      } else {
        parse_err(src, "invalid memory operand", ctx);
      }
    }
  }
}

static token_t* parse_operands(token_t* src, parsed_operands_t* dst, parse_ctx_t* ctx) {
  if (src->type == TOK_PUNCT(';')) {
    return ++src;
  }
  uint32_t seen = 0;
  for (;;) {
    uint32_t tt = src->type;
    if (tt < TOK_IDENT_THR) {
      // Could be register name, or size specifier, or symbol name.
      uint32_t info = *(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt) & 0xff;
      if (info) {
        info ^= OP_SPECIAL_NOTHING;
        if (info <= OP_SPECIAL_LAST_REG) {
          dst->op[dst->n++] = info;
          ++src;
          goto got_operand;
        } else if (info >= OP_SPECIAL_FIRST_SIZE_KW) {
          dst->modrm_etc |= MODRM_ETC_SIZE_KNOWN | ((info & 7) << 5);
          ++src;
          tt = src->type;
          if (tt < TOK_IDENT_THR) {
            info = *(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt) & 0xff;
            if (info == (OP_SPECIAL_KW_PTR ^ OP_SPECIAL_NOTHING)) {
              ++src;
              tt = src->type;
            }
          }
          if (tt != TOK_PUNCT('[')) {
            parse_err(src, "expected [ to follow memory size keyword", ctx);
          }
          goto mem_operand;
        }
      }
      if (seen & 2) goto mem_operand;
      dst->op[dst->n++] = OP_SPECIAL_SYMBOL;
      dst->imm = tt;
      ++src;
      goto got_operand;
    } else if (tt == TOK_PUNCT('[')) { mem_operand:
      if (seen & 2) {
        parse_err(src, "instruction already has a memory operand", ctx);
      }
      seen |= 2;
      src = parse_mem(src + 1, dst, ctx);
      dst->op[dst->n++] = OP_SPECIAL_MEM;
    } else if (tt == TOK_NUMBER || tt == TOK_PUNCT('-') || tt == TOK_PUNCT('+') || tt == TOK_CHAR_LIT || tt == TOK_CHAR_LIT_ESC) {
      if (seen & 1) {
        parse_err(src, "instruction already has an immediate operand", ctx);
      }
      seen |= 1;
      src = parse_imm(src, &dst->imm, ctx);
      dst->op[dst->n++] = OP_SPECIAL_IMM;
    } else {
      if (dst->n == 0) {
        break;
      }
      parse_err(src, "invalid instruction operand", ctx);
    }
  got_operand:
    if (src->type != TOK_PUNCT(',')) {
      break;
    }
    if (dst->n == sizeof(dst->op)/sizeof(dst->op[0])) {
      parse_err(src, "instruction has too many operands", ctx);
    }
    ++src;
  }
  return src;
}

#define CMD_CFI 0x80
#define CMD_reloc 0xC0

#define CFI_REG_RBP 6
#define CFI_REG_RSP 7
static uint8_t arch_reg_to_cfi_reg[8] = {
  0, 2, 1, 3, 7, 6, 4, 5
};
static int is_non_vol_reg(uint32_t arch_idx) {
  return (0xf028u >> (arch_idx & 31)) & 1;
}

static uint8_t* write_cfi_op_uleb(uint8_t* out, uint8_t opcode, uint32_t operand) {
  if (operand <= 0x7f) {
    *out++ = CMD_CFI + 2;
    *out++ = opcode;
    *out++ = operand;
  } else if (operand <= 0x3fff) {
    *out++ = CMD_CFI + 3;
    *out++ = opcode;
    *out++ = (operand & 0x7f) | 0x80;
    *out++ = (operand >> 7);
  } else {
    *out++ = CMD_CFI + 4;
    *out++ = opcode;
    *out++ = (operand & 0x7f) | 0x80;
    *out++ = ((operand >> 7) & 0x7f) | 0x80;
    *out++ = (operand >> 14);
  }
  return out;
}

#define DW_CFA_remember_state   0x0a
#define DW_CFA_restore_state    0x0b
#define DW_CFA_def_cfa          0x0c
#define DW_CFA_def_cfa_register 0x0d
#define DW_CFA_def_cfa_offset   0x0e
#define DW_CFA_offset           0x80
#define DW_CFA_restore          0xc0

static void encode_insn_cfi(const uint8_t* bc, parsed_operands_t* operands, parse_ctx_t* ctx, token_t* where) {
  uint8_t* out = ensure_payloads_space(ctx, 10);
  bc += operands->n;
  uint32_t n = *bc++ & ENC_LENGTH_MASK;
  if (n) {
    switch (*bc) {
    case 0x50: {
      // push r64
      ctx->cfi.n_push += 1;
      if (!ctx->cfi.n_push) {
        parse_err(where, "CFI stack overflow", ctx);
      }
      if (ctx->cfi.cfa_reg == CFI_REG_RSP) {
        out = write_cfi_op_uleb(out, DW_CFA_def_cfa_offset, ctx->cfi.n_push * 8);
      }
      uint32_t reg = operands->op[0];
      if (is_non_vol_reg(reg)) {
        if ((reg >> 5) != 3) {
          parse_err(where, "push of non-volatile register should be 64-bit", ctx);
        }
        reg &= 31;
        if (reg < sizeof(arch_reg_to_cfi_reg)) {
          reg = arch_reg_to_cfi_reg[reg];
        }
        if (!(ctx->cfi.saved_mask & (1u << reg))) {
          ctx->cfi.saved_mask |= (1u << reg);
          out = write_cfi_op_uleb(out, DW_CFA_offset + reg, ctx->cfi.n_push);
        }
      }
      ctx->payloads_size = out - ctx->payloads;
      return; }
    case 0x58: {
      // pop r64
      if (ctx->cfi.n_push <= 1) {
        parse_err(where, "CFI stack underflow", ctx);
      }
      ctx->cfi.n_push -= 1;
      if (ctx->cfi.cfa_reg == CFI_REG_RSP) {
        out = write_cfi_op_uleb(out, DW_CFA_def_cfa_offset, ctx->cfi.n_push * 8);
      }
      uint32_t reg = operands->op[0];
      if (is_non_vol_reg(reg)) {
        if ((reg >> 5) != 3) {
          parse_err(where, "pop of non-volatile register should be 64-bit", ctx);
        }
        reg &= 31;
        if (reg < sizeof(arch_reg_to_cfi_reg)) {
          reg = arch_reg_to_cfi_reg[reg];
        }
        if (ctx->cfi.saved_mask & (1u << reg)) {
          ctx->cfi.saved_mask -= (1u << reg);
          *out++ = CMD_CFI + 1;
          *out++ = DW_CFA_restore + reg;
        }
        if (reg == ctx->cfi.cfa_reg) {
          ctx->cfi.cfa_reg = CFI_REG_RSP;
          *out++ = CMD_CFI + 1;
          *out++ = DW_CFA_def_cfa;
          out = write_cfi_op_uleb(out, CFI_REG_RSP, ctx->cfi.n_push * 8);
        }
      }
      ctx->payloads_size = out - ctx->payloads;
      return; }
    case 0x8b:
      // mov r rm
      if (operands->op[0] == PACK_OP(3, 5) && operands->op[1] == PACK_OP(3, 4)) {
        // mov rbp, rsp
        if (ctx->cfi.n_push != 2 || ctx->cfi.saved_mask != (1u << CFI_REG_RBP) || ctx->cfi.cfa_reg != CFI_REG_RSP) {
          parse_err(where, "mov.cfi rbp, rsp with unexpected CFI state", ctx);
        }
        out = write_cfi_op_uleb(out, DW_CFA_def_cfa_register, CFI_REG_RBP);
        ctx->cfi.cfa_reg = CFI_REG_RBP;
        ctx->payloads_size = out - ctx->payloads;
        return;
      }
      break;
    case 0xc9:
      // leave; equivalent to `mov rsp, rbp; pop rbp`.
      if (ctx->cfi.n_push < 2 || ctx->cfi.saved_mask != (1u << CFI_REG_RBP) || ctx->cfi.cfa_reg != CFI_REG_RBP) {
        parse_err(where, "leave.cfi with unexpected CFI state", ctx);
      }
      ctx->cfi.n_push = 1;
      ctx->cfi.saved_mask = 0;
      ctx->cfi.cfa_reg = CFI_REG_RSP;
      *out++ = CMD_CFI + 4;
      *out++ = DW_CFA_restore + CFI_REG_RBP;
      *out++ = DW_CFA_def_cfa;
      *out++ = CFI_REG_RSP;
      *out++ = 8;
      ctx->payloads_size = out - ctx->payloads;
      return;
    }
  }
  parse_err(where, "unsupported .cfi suffix", ctx);
}

static void encode_insn(const uint8_t* bc, parsed_operands_t* operands, parse_ctx_t* ctx) {
  uint8_t opcode_mod = 0;
  uint8_t rex = 0; // high bit set if rex prohibited, next bit set if rex prefix required, low four bits are w/r/x/b
  uint8_t modrm = 0; // high bit set if o16 prefix required, next bit set if modrm prefix required, then low 6 bits of modrm payload
  uint8_t reloc = 0;
  if (operands->inferred_size & INFERRED_SIZE_KNOWN) {
    switch (operands->inferred_size >> 5) {
    case 0: // inferred size was 8-bit
      opcode_mod = 0xff; // decrement opcode by 1
      break;
    case 1: // inferred size was 16-bit
      modrm |= 0x80; // need o16 prefix
      break;
    case 2: // inferred size was 32-bit
      break;
    case 3: // inferred size was 64-bit
      rex |= 0x48; // need rex.w
      break;
    default:
      FATAL("TODO: vex/evex width encoding");
    }
  }
  for (uint32_t i = 0; i < operands->n; ++i) {
    uint32_t info = bc[i];
    uint32_t actual = operands->op[i];
    if (actual <= OP_SPECIAL_LAST_REG) {
      if (actual < PACK_OP(0, 4)) {
        // fine
      } else if (actual < PACK_OP(1, 0)) {
        rex |= 0x40; // need rex to address byte registers beyond [abcd]l
      } else if (actual < PACK_SPECIAL(0, 0)) {
        actual &= 0x1f;
        if (actual & 0x10) FATAL("TODO: 32 registers");
      } else if (actual < PACK_SPECIAL(SPECIAL_KIND_H_REG, 0)) {
        actual &= 7;
      } else {
        rex |= 0x80; // rex disallowed if using [abcd]h
        actual = 4 + (actual & 3);
      }
    }
    switch (info >> 4) {
    case (OP_CLASS_R << 2) + R_ENCODE_STYLE_MODRM_REG:
      if (actual & 8) rex |= 0x44; // 4th bit in rex.r
      modrm |= 0x40 | ((actual & 7) << 3);
      break;
    case (OP_CLASS_R << 2) + R_ENCODE_STYLE_ADD_TO_OPCODE:
      if (actual & 8) rex |= 0x41; // 4th bit in rex.b
      opcode_mod += (actual & 7);
      break;
    case (OP_CLASS_R << 2) + R_ENCODE_STYLE_V:
      FATAL("TODO: vex encoding");
      break;
    case (OP_CLASS_R << 2) + R_ENCODE_STYLE_IMPLICIT:
      break;
    case (OP_CLASS_R << 2) + R_ENCODE_STYLE_MODRM_RM: rm_is_reg:
      operands->modrm_base = actual;
      operands->modrm_index = OP_SPECIAL_NOTHING;
      operands->modrm_imm = 0;
      modrm |= 0x40;
      break;
    case (OP_CLASS_R << 2) + R_ENCODE_STYLE_KMASK:
      FATAL("TODO: evex encoding");
      break;
    case (OP_CLASS_R << 2) + R_ENCODE_STYLE_IMM4:
      operands->imm = actual << 4;
      operands->inferred_size = (operands->inferred_size & 0xf8) | IMM_SIZE_CLASS_I8;
      break;
    case (OP_CLASS_RM << 2) + 0:
      if (actual != OP_SPECIAL_MEM) goto rm_is_reg;
      // fallthrough
    case (OP_CLASS_M << 2) + M_ENCODE_STYLE_MODRM_RM:
      modrm |= 0x40;
      break;
    case (OP_CLASS_M << 2) + M_ENCODE_STYLE_IMPLICIT_RDI:
    case (OP_CLASS_M << 2) + M_ENCODE_STYLE_IMPLICIT_RSI:
      break;
    case (OP_CLASS_M << 2) + M_ENCODE_STYLE_RELOC:
      reloc = CMD_reloc + (info & 0xf);
      operands->inferred_size |= IMM_SIZE_CLASS_I32;
      break;
    case (OP_CLASS_IMM << 2) + 0:
      // Nothing to do.
      break;
    default:
      FATAL("Bad class/encoding combination");
    }
  }
  bc += operands->n;
  uint32_t info = *bc++; // 1 bit force rex.w, 1 bit has /n, 5 bits # of opcode bytes
  if (info & ENC_FORCE_REX_W) {
    rex |= 0x48;
  }
  if (info & ENC_HAS_MODRM_NIBBLE) {
    modrm |= bc[info & ENC_LENGTH_MASK]; // provides middle three bits
  }
  uint8_t* out = ensure_payloads_space(ctx, 32);
  uint8_t* out_start = ++out;
  if (modrm & 0x80) {
    *out++ = 0x66; // o16 prefix
  }
  if (modrm & 0x40) {
    if (operands->modrm_base <= OP_SPECIAL_LAST_REG) {
      if (operands->modrm_base & 0x10) FATAL("TODO: 32 registers");
      if (operands->modrm_base & 8) rex |= 0x41; // 4th bit in rex.b
    }
    if (operands->modrm_index <= OP_SPECIAL_LAST_REG) {
      if (operands->modrm_index & 0x10) FATAL("TODO: 32 registers");
      if (operands->modrm_index & 8) rex |= 0x42; // 4th bit in rex.x
    }
  }
  if (info & ENC_LENGTH_MASK) {
    // If there is a mandatory prefix as part of the encoding, it needs to be
    // emitted prior to the rex prefix.
    uint8_t head = *bc;
    if (head == 0x66 || head == 0xf2 || head == 0xf3) {
      *out++ = head;
      --info;
      ++bc;
    }
  }
  if (rex & 0x7f) {
    if (rex & 0x80) FATAL("rex both required and prohibited");
    *out++ = rex; // rex prefix
  }
  memcpy(out, bc, info & ENC_LENGTH_MASK);
  bc += (info & ENC_LENGTH_MASK);
  out += (info & ENC_LENGTH_MASK);
  if (opcode_mod) {
    out[-1] += opcode_mod;
  }
  if (modrm & 0x40) {
    modrm &= 0x38;
    if (operands->modrm_base < PACK_OP(1, 0)) {
      *out++ = 0xC0 | modrm | (operands->modrm_base & 7);
    } else if (operands->modrm_base == OP_SPECIAL_SYMBOL) {
      *out++ = modrm | 5;
      modrm |= 0x80; // 32-bit disp
      reloc = CMD_reloc + (operands->inferred_size & INFERRED_SIZE_IMM_SIZE_MASK);
    } else {
      if (operands->modrm_base == OP_SPECIAL_NOTHING || operands->modrm_index != OP_SPECIAL_NOTHING) {
        modrm |= 4;
      } else {
        modrm |= operands->modrm_base & 7;
      }
      if (operands->modrm_imm != 0 || ((operands->modrm_base & 7) == 5)) {
        if (operands->modrm_base != OP_SPECIAL_NOTHING) {
          if ((int64_t)operands->modrm_imm == (int8_t)(uint8_t)operands->modrm_imm) {
            modrm |= 0x40; // 8-bit disp
          } else {
            modrm |= 0x80; // 32-bit disp
          }
        }
      }
      *out++ = modrm;
      if ((modrm & 7) == 4) {
        uint32_t sib;
        if (operands->modrm_base == OP_SPECIAL_NOTHING) {
          sib = 5;
          modrm |= 0x80; // 32-bit disp
        } else {
          sib = operands->modrm_base & 7;
        }
        if (operands->modrm_index == OP_SPECIAL_NOTHING) {
          sib |= 0x20;
        } else {
          sib |= ((operands->modrm_etc & MODRM_ETC_SCALE_MASK) << 6) | ((operands->modrm_index & 7) << 3);
        }
        *out++ = sib;
      }
    }
    if (modrm & 0x80) {
      uint32_t u32 = (uint32_t)operands->modrm_imm;
      memcpy(out, &u32, sizeof(u32));
      out += sizeof(u32);
    } else if (modrm & 0x40) {
      *out++ = (uint8_t)operands->modrm_imm;
    }
  }
  switch (operands->inferred_size & INFERRED_SIZE_IMM_SIZE_MASK) {
  case 0:
    break;
  case IMM_SIZE_CLASS_I8:
    *out++ = (uint8_t)operands->imm;
    break;
  case IMM_SIZE_CLASS_I16: {
    uint16_t u16 = operands->imm;
    memcpy(out, &u16, sizeof(u16));
    out += sizeof(u16);
    break; }
  case IMM_SIZE_CLASS_I32: {
    uint32_t u32 = operands->imm;
    memcpy(out, &u32, sizeof(u32));
    out += sizeof(u32);
    break; }
  case IMM_SIZE_CLASS_I64:
    memcpy(out, &operands->imm, sizeof(operands->imm));
    out += sizeof(operands->imm);
    break;
  default:
    FATAL("Bad imm size");
    break;
  }
  out_start[-1] = out - out_start;
  if (reloc) {
    *out++ = reloc;
  }
  ctx->payloads_size = out - ctx->payloads;
}

static bool is_operand_ok(parsed_operands_t* operands, uint32_t idx, uint32_t bc) {
  uint32_t clazz = bc >> 6;
  uint32_t enc = (bc >> 4) & 3;
  uint32_t sz = bc & 0xf;
  uint32_t actual = operands->op[idx];
  switch (clazz) {
  case OP_CLASS_RM:
    if (actual == OP_SPECIAL_MEM && !enc) goto clazz_m;
    // fallthrough
  case OP_CLASS_R:
    if (actual > OP_SPECIAL_LAST_REG) return false;
    switch (sz) {
    case 0:
      if (PACK_SPECIAL(SPECIAL_KIND_H_REG, 0) <= actual && actual <= PACK_SPECIAL(SPECIAL_KIND_H_REG, 3)) return true;
      // fallthrough
    case 1: case 2: case 3: case 4: case 5: case 6:
      return (actual >> 5) == sz;
    case SIZE_CLASS_X87:
      return PACK_SPECIAL(SPECIAL_KIND_ST_REG, 0) <= actual && actual <= PACK_SPECIAL(SPECIAL_KIND_ST_REG, 7);
    case SIZE_CLASS_K:
      // TODO k0 not allowed if encoding to opmask field
      return PACK_SPECIAL(SPECIAL_KIND_K_REG, 0) <= actual && actual <= PACK_SPECIAL(SPECIAL_KIND_K_REG, 7);
    case SIZE_CLASS_CL:
      return actual == PACK_OP(0, 1);
    case SIZE_CLASS_ST_0:
      return actual == PACK_SPECIAL(SPECIAL_KIND_ST_REG, 0);
    case SIZE_CLASS_XMM_0:
      return actual == PACK_OP(4, 0);
    case SIZE_CLASS_GPR_0:
      if (actual & 0x9f) return false;
      actual >>= 5;
      break;
    case SIZE_CLASS_GPR:
      if (PACK_SPECIAL(SPECIAL_KIND_H_REG, 0) <= actual && actual <= PACK_SPECIAL(SPECIAL_KIND_H_REG, 3)) {
        actual = 0;
      }
      actual >>= 5;
      if (actual > 3) return false;
      break;
    case SIZE_CLASS_GPR_NO8:
      actual >>= 5;
      if (actual == 0 || actual > 3) return false;
      break;
    case SIZE_CLASS_XYZ:
      actual >>= 5;
      if (actual < 4 || actual > 6) return false;
      break;
    }
    if (operands->inferred_size & INFERRED_SIZE_KNOWN) {
      return (operands->inferred_size >> 5) == actual;
    } else {
      operands->inferred_size |= (actual << 5) | INFERRED_SIZE_KNOWN;
      return true;
    }
  case OP_CLASS_M:
    if (actual != OP_SPECIAL_MEM) {
      return (enc == M_ENCODE_STYLE_RELOC) && (actual == OP_SPECIAL_SYMBOL);
    }
    if (enc) {
      if (enc == M_ENCODE_STYLE_RELOC || operands->modrm_imm != 0 || operands->modrm_index != OP_SPECIAL_NOTHING || operands->modrm_base >= PACK_OP(4, 0) || (operands->modrm_base & 0x1f) != (6 + M_ENCODE_STYLE_IMPLICIT_RSI + enc)) {
        return false;
      }
    }
  clazz_m:
    if (operands->modrm_base < PACK_OP(3, 0) || operands->modrm_base >= PACK_OP(4, 0)) {
      if (operands->modrm_base != OP_SPECIAL_NOTHING && operands->modrm_base != OP_SPECIAL_SYMBOL) {
        return false;
      }
    }
    if (operands->modrm_index < PACK_OP(3, 0) || operands->modrm_index >= PACK_OP(4, 0)) {
      if (operands->modrm_index != OP_SPECIAL_NOTHING) {
        return false;
      }
    }
    if (operands->modrm_base != OP_SPECIAL_SYMBOL && (int64_t)operands->modrm_imm != (int32_t)(uint32_t)operands->modrm_imm) {
      return false;
    }
    switch (sz) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case SIZE_CLASS_X87:
      if (operands->modrm_etc & MODRM_ETC_SIZE_KNOWN) {
        return (operands->modrm_etc >> 5) == sz;
      } else {
        operands->modrm_etc |= (sz << 5) | MODRM_ETC_SIZE_KNOWN;
        return true;
      }
    case SIZE_CLASS_K:
    case SIZE_CLASS_CL:
    case SIZE_CLASS_ST_0:
    case SIZE_CLASS_XMM_0:
    case SIZE_CLASS_GPR_0:
      FATAL("Unexpected size class for memory operand");
      return false;
    case SIZE_CLASS_GPR:
      sz = 0; actual = 3;
      break;
    case SIZE_CLASS_GPR_NO8:
      sz = 1; actual = 3;
      break;
    case SIZE_CLASS_XYZ:
      sz = 4; actual = 6;
      break;
    }
    if (operands->modrm_etc & MODRM_ETC_SIZE_KNOWN) {
      if ((operands->modrm_etc >> 5) < sz || (operands->modrm_etc >> 5) > actual) return false;
      if (operands->inferred_size & INFERRED_SIZE_KNOWN) {
        return (operands->inferred_size >> 5) == (operands->modrm_etc >> 5);
      } else {
        operands->inferred_size |= (operands->modrm_etc & 0xf0);
        return true;
      }
    } else {
      if (operands->inferred_size & INFERRED_SIZE_KNOWN) {
        if ((operands->inferred_size >> 5) < sz || (operands->inferred_size >> 5) > actual) return false;
        operands->modrm_etc |= (operands->inferred_size & 0xf0);
        return true;
      } else {
        operands->inferred_size |= INFERRED_SIZE_NEED_RETRY;
        return true;
      }
    }
  case OP_CLASS_IMM:
    if (actual != OP_SPECIAL_IMM) return false;
    switch (sz) {
    case IMM_SIZE_CLASS_I8: imm8:
      if ((int64_t)operands->imm != (int8_t)(uint8_t)operands->imm) return false;
      break;
    case IMM_SIZE_CLASS_I16: imm16:
      if ((int64_t)operands->imm != (int16_t)(uint16_t)operands->imm) return false;
      break;
    case IMM_SIZE_CLASS_I32: imm32:
      if ((int64_t)operands->imm != (int32_t)(uint32_t)operands->imm) {
        if (operands->imm != (uint32_t)operands->imm || (int32_t)(uint32_t)operands->imm >= 0) {
          return false;
        }
      }
      break;
    case IMM_SIZE_CLASS_I64:
      break;
    case IMM_SIZE_CLASS_CONSTANT_1:
      return operands->imm == 1;
    case IMM_SIZE_CLASS_INFERRED_8_16_32:
      if (!(operands->inferred_size & INFERRED_SIZE_KNOWN)) {
        operands->inferred_size |= INFERRED_SIZE_NEED_RETRY;
        return true;
      }
      sz = operands->inferred_size >> 5;
      if (sz == 2 || sz == 3) { sz = IMM_SIZE_CLASS_I32; goto imm32; }
      else if (sz == 1) { sz = IMM_SIZE_CLASS_I16; goto imm16; }
      else if (sz == 0) { sz = IMM_SIZE_CLASS_I8; goto imm8; }
      else return false;
    default:
      FATAL("Bad size class for imm");
      return false;
    }
    operands->inferred_size &= 0xf8;
    operands->inferred_size |= sz;
    return true;
  default:
    FATAL("Bad class");
    return false;
  }
}

static const uint8_t* match_insn_overload(const uint8_t* bc, parsed_operands_t* operands) {
  uint32_t n = operands->n;
  uint32_t i;
  operands->inferred_size = 0;
  for (;;) {
  retry:
    for (i = 0; i < n; ++i) {
      if (!is_operand_ok(operands, i, bc[i])) {
        goto next;
      }
    }
    if (operands->inferred_size & INFERRED_SIZE_NEED_RETRY) {
      if (operands->inferred_size & INFERRED_SIZE_KNOWN) {
        operands->inferred_size -= INFERRED_SIZE_NEED_RETRY;
        goto retry;
      }
      return false;
    }
    return bc;
  next:
    i = bc[-1];
    if (!i) return NULL;
    bc += i;
  }
}

static void funge_operands(parsed_operands_t* operands, uint32_t funge_kind) {
  switch (funge_kind) {
  case FUNGE_lea: {
    uint32_t rhs = operands->op[1];
    if (rhs == OP_SPECIAL_MEM) {
      // Registers in memory operand can have size of destination operand.
      uint32_t lo = operands->op[0] & 0xf0;
      if (lo <= operands->modrm_base && operands->modrm_base < PACK_OP(3, 0)) {
        operands->modrm_base |= PACK_OP(3, 0);
      }
      if (lo <= operands->modrm_index && operands->modrm_index < PACK_OP(3, 0)) {
        operands->modrm_index |= PACK_OP(3, 0);
      }
      // Ignore size on memory operand.
      operands->modrm_etc &=~ MODRM_ETC_SIZE_KNOWN;
    } else if (rhs == OP_SPECIAL_SYMBOL) {
      // Allow sym as syntax sugar for [&sym].
      operands->op[1] = OP_SPECIAL_MEM;
      operands->modrm_base = OP_SPECIAL_SYMBOL;
      operands->modrm_index = OP_SPECIAL_NOTHING;
      operands->modrm_imm = operands->imm;
      operands->imm = 0;
    }
    break; }
  case FUNGE_xor: {
    // When operands are identical, can narrow 64-bit to 32-bit.
    uint32_t op = operands->op[0];
    if (op == operands->op[1] && PACK_OP(3, 0) <= op && op < PACK_OP(4, 0)) {
      op ^= PACK_OP(2, 0) ^ PACK_OP(3, 0);
      operands->op[0] = operands->op[1] = op;
    }
    break; }
  case FUNGE_mov:
    if (operands->op[1] == OP_SPECIAL_IMM) {
      uint32_t lhs = operands->op[0];
      if (PACK_OP(3, 0) <= lhs && lhs < PACK_OP(4, 0) && operands->imm == (uint32_t)operands->imm) {
        // mov r64, u32 -> mov r32, u32
        lhs ^= PACK_OP(2, 0) ^ PACK_OP(3, 0);
        operands->op[0] = lhs;
      }
      if (PACK_OP(2, 0) <= lhs && lhs < PACK_OP(3, 0)) {
        // mov r32, imm ignores high 32 bits of imm, so normalise them away
        operands->imm = (int64_t)(int32_t)(uint32_t)operands->imm;
      }
    }
    break;
  case FUNGE_test:
  case FUNGE_and:
    if (operands->op[1] == OP_SPECIAL_IMM) {
      uint32_t lhs = operands->op[0];
      uint64_t imm = operands->imm;
      if (PACK_OP(3, 0) <= lhs && lhs < PACK_OP(4, 0)) {
        if (imm <= 0x7fffffff) {
          lhs ^= PACK_OP(2, 0) ^ PACK_OP(3, 0);
        }
      }
      if (PACK_OP(2, 0) <= lhs && lhs < PACK_OP(3, 0)) {
        imm = (int64_t)(int32_t)(uint32_t)imm; // Only low 32 bits of imm used, normalise away the rest.
        if ((uint32_t)imm <= 0x7fff && funge_kind == FUNGE_test) {
          lhs ^= PACK_OP(1, 0) ^ PACK_OP(2, 0);
        }
      }
      if (PACK_OP(1, 0) <= lhs && lhs < PACK_OP(2, 0)) {
        imm = (int64_t)(int16_t)(uint16_t)imm; // Only low 16 bits of imm used, normalise away the rest.
        if ((uint16_t)imm <= 0x7f && funge_kind == FUNGE_test) {
          lhs ^= PACK_OP(0, 0) ^ PACK_OP(1, 0);
        }
      }
      if (lhs < PACK_OP(1, 0)) {
        imm = (int64_t)(int8_t)(uint8_t)imm; // Only low 8 bits of imm used, normalise away the rest.
      }
      operands->op[0] = lhs;
      operands->imm = imm;
    }
    break;
  }
}

static void encode_label(uint32_t label, parse_ctx_t* ctx) {
  uint8_t* out = ensure_payloads_space(ctx, 2 + sizeof(label));
  *out++ = sizeof(label);
  memcpy(out, &label, sizeof(label));
  out += sizeof(label);
  *out++ = CMD_reloc + RELOC_def_label;
  ctx->payloads_size = out - ctx->payloads;
}

static token_t* parse_asciiz(token_t* src, parse_ctx_t* ctx) {
  for (;;) {
    uint32_t src_len;
    const char* src_s = tokeniser_decode_str_lit(&ctx->toks, src, &src_len);
    if (!src_s) {
      parse_err(src, "expected string literal as asciiz operand", ctx);
    }
    uint32_t n = src_len + 1;
    uint8_t* out = ensure_payloads_space(ctx, n + 2);
    memcpy(out + 1, src_s, n - 1);
    if (n & (n - 1)) {
      *out++ = n;
      out += n;
      out[-1] = 0;
    } else {
      *out++ = n - 1;
      out += n;
      out[-1] = 1;
      *out++ = 0;
    }
    ctx->payloads_size = out - ctx->payloads;
    ++src;
    if (src->type != TOK_PUNCT(',')) break;
    ++src;
  }
  return src;
}

static token_t* parse_cfi_byte(token_t* src, parse_ctx_t* ctx) {
  for (;;) {
    uint64_t val;
    src = parse_imm(src, &val, ctx);
    uint8_t* out = ensure_payloads_space(ctx, 2);
    *out++ = CMD_CFI + 1;
    *out++ = (uint8_t)val;
    ctx->payloads_size = out - ctx->payloads;
    if (src->type != TOK_PUNCT(',')) break;
    ++src;
  }
  return src;
}

static token_t* parse_data(token_t* src, parse_ctx_t* ctx) {
  uint32_t tt = src->type;
  uint8_t imm_len;
  switch (tt) {
  case KW_byte:
    imm_len = 1;
    break;
  case KW_qword:
    imm_len = 8;
    break;
  case KW_asciiz:
    return parse_asciiz(src + 1, ctx);
    break;
  default:
    parse_err(src, "expected byte/qword/asciiz keyword", ctx);
    return src;
  }
  ++src;
  for (;;) {
    uint64_t val;
    src = parse_imm(src, &val, ctx);
    uint8_t* out = ensure_payloads_space(ctx, 1 + imm_len);
    *out++ = imm_len;
    if (imm_len == 1) {
      *out++ = (uint8_t)val;
    } else if (imm_len == 8) {
      memcpy(out, &val, 8);
      out += 8;
    } else {
      FATAL("Unexpected imm_len of %u", (unsigned)imm_len);
    }
    ctx->payloads_size = out - ctx->payloads;
    if (src->type != TOK_PUNCT(',')) break;
    ++src;
  }
  return src;
}

static token_t* parse_phantom_ref(token_t* src, parse_ctx_t* ctx) {
  if (src->type >= TOK_IDENT_THR) {
    parse_err(src, "expected identifier as operand to phantom_ref", ctx);
  }
  uint8_t* out = ensure_payloads_space(ctx, 2 + sizeof(uint32_t));
  *out++ = sizeof(uint32_t);
  memcpy(out, &src->type, sizeof(uint32_t));
  out += sizeof(uint32_t);
  *out++ = CMD_reloc + RELOC_phantom_ref;
  ctx->payloads_size = out - ctx->payloads;
  return src + 1;
}

static token_t* parse_align(token_t* src, parse_ctx_t* ctx) {
  uint64_t amount;
  src = parse_imm(src, &amount, ctx);
  if (amount == 0 || amount > 0x100 || amount & (amount - 1)) {
    parse_err(src - 1, "bad align value", ctx);
  }
  uint8_t* out = ensure_payloads_space(ctx, 3);
  *out++ = 1;
  *out++ = (uint8_t)(amount - 1);
  *out++ = CMD_reloc + RELOC_align;
  ctx->payloads_size = out - ctx->payloads;
  return src;
}

static token_t* parse_u8_label_lut(token_t* src, parse_ctx_t* ctx) {
  uint8_t* out = ensure_payloads_space(ctx, 3 + 8 * sizeof(uint32_t));
  *out++ = CMD_reloc + RELOC_u8_label_lut;
  if (src->type != TOK_PUNCT('(')) {
    parse_err(src, "expected ( after u8_label_lut", ctx);
  }
  ++src;
  uint32_t tt = src->type;
  if (tt < TOK_IDENT_THR) {
    uint32_t info = *(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt) & 0xff;
    if (info) {
      info ^= OP_SPECIAL_NOTHING;
      if (PACK_OP(3, 0) <= info && info <= PACK_OP(3, 15)) {
        out[1] = info & 15;
        goto got_reg1;
      }
    }
  }
  parse_err(src, "expected 64-bit GPR as 1st operand of u8_label_lut", ctx);
got_reg1:
  ++src;
  if (src->type != TOK_PUNCT(',')) {
    parse_err(src, "expected , as part of u8_label_lut", ctx);
  }
  ++src;
  tt = src->type;
  if (tt < TOK_IDENT_THR) {
    uint32_t info = *(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt) & 0xff;
    if (info == (PACK_OP(2, 1) ^ OP_SPECIAL_NOTHING) || info == (PACK_OP(3, 1) ^ OP_SPECIAL_NOTHING)) {
      goto got_reg2;
    }
  }
  parse_err(src, "expected ecx or rcx as 2nd operand of u8_label_lut", ctx);
got_reg2:
  ++src;
  uint8_t* out_head = out + 1;
  out += 2;
  while (src->type == TOK_PUNCT(',')) {
    ++src;
    tt = src->type;
    if (tt >= TOK_IDENT_THR) {
      parse_err(src, "expected label name as operand of u8_label_lut", ctx);
    }
    memcpy(out, &tt, sizeof(tt));
    out += sizeof(tt);
    ++src;
  }
  out_head[-1] = out - out_head;
  ctx->payloads_size = out - ctx->payloads;
  if (src->type != TOK_PUNCT(')')) {
    parse_err(src, "expected ) to conclude u8_label_lut", ctx);
  }
  ++src;
  return src;
}

static token_t* parse_simple_cfi(token_t* src, parse_ctx_t* ctx, uint8_t opcode) {
  uint8_t* out = ensure_payloads_space(ctx, 2);
  *out++ = CMD_CFI + 1;
  *out++ = opcode;
  ctx->payloads_size = out - ctx->payloads;
  switch (opcode) {
  case DW_CFA_remember_state:
    memcpy(&ctx->pushed_cfi, &ctx->cfi, sizeof(ctx->cfi));
    break;
  case DW_CFA_restore_state:
    memcpy(&ctx->cfi, &ctx->pushed_cfi, sizeof(ctx->cfi));
    break;
  }
  return src;
}

static token_t* parse_insn(token_t* src, parse_ctx_t* ctx) {
  uint32_t tt = src->type;
  if (tt >= TOK_IDENT_THR) { not_insn:
    parse_err(src, "expected instruction name", ctx);
  }
  if (src[1].type == TOK_PUNCT(':')) {
    encode_label(tt, ctx);
    return src + 2;
  }
  uint32_t info = (*(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt)) >> 8;
  if (!info) {
    switch (tt) {
    case KW_align: return parse_align(src + 1, ctx);
    case KW_u8_label_lut: return parse_u8_label_lut(src + 1, ctx);
    case KW_phantom_ref: return parse_phantom_ref(src + 1, ctx);
    case KW_cfi_byte: return parse_cfi_byte(src + 1, ctx);
    case KW_cfi_remember_state: return parse_simple_cfi(src + 1, ctx, DW_CFA_remember_state);
    case KW_cfi_restore_state: return parse_simple_cfi(src + 1, ctx, DW_CFA_restore_state);
    }
    goto not_insn;
  }
  token_t* insn_name = src;
  ++src;
  token_t* with_cfi = NULL;
  if (src->type == TOK_PUNCT('.') && src[1].type == KW_cfi) {
    with_cfi = src + 1;
    src += 2;
  }
  const uint8_t* bc = insn_encodings + info;
  uint32_t narg = *bc++;
  parsed_operands_t operands;
  operands.n = 0;
  operands.inferred_size = 0;
  operands.modrm_etc = 0;
  if (narg) {
    src = parse_operands(src, &operands, ctx);
    uint32_t narg_lo = narg & 7;
    uint32_t narg_hi = narg >> 4;
    if (operands.n < narg_lo) {
      parse_err(insn_name, "not enough operands", ctx);
    }
    if (operands.n > narg_hi) {
      parse_err(insn_name, "too many operands", ctx);
    }
    if (narg & 8) {
      funge_operands(&operands, *bc++);
    }
    uint8_t delta = 1;
    if (narg_lo != narg_hi) {
      delta = bc[operands.n - narg_lo];
      if (!delta) {
        parse_err(insn_name, "wrong number of operands", ctx);
      }
    }
    bc = match_insn_overload(bc + delta, &operands);
    if (!bc) {
      parse_err(insn_name, "operands incompatible with instruction", ctx);
    }
  }
  encode_insn(bc, &operands, ctx);
  if (with_cfi) {
    encode_insn_cfi(bc, &operands, ctx, with_cfi);
  }
  return src;
}

static void enter_insns(sht_t* sht) {
  const uint8_t* src = insn_encodings;
  for (;;) {
    uint8_t slen = src[0];
    uint8_t dlen = src[1];
    if (!slen) break;
    src += 2;
    *(uint32_t*)sht_intern_p(sht, (const char*)src, slen) |= (src + slen - insn_encodings) << 8;
    src += slen;
    src += dlen;
  }
}

static void enter_operands(sht_t* sht) {
  const char* src = operand_keywords;
  for (;;) {
    uint8_t slen = (uint8_t)src[0];
    if (!slen) break;
    uint8_t val = (uint8_t)src[1];
    src += 2;
    *(uint32_t*)sht_intern_p(sht, (const char*)src, slen) |= val;
    src += slen;
  }
}

static const char* find_colon_colon(const char* start, const char* end) {
  size_t n = end - start;
  for (;;) {
    if (n < 2) break;
    start = (char*)memchr(start, ':', n);
    if (!start) break;
    n = end - start;
    if (n < 2) break;
    if (start[1] == ':') return start;
    start += 2;
    n -= 2;
  }
  return NULL;
}

static void encode_extern(parse_ctx_t* ctx, asm_sym_t* sym, token_t* t) {
  (void)ensure_payloads_space(ctx, t->end - t->start + 16);
  sym->payload_start = ctx->payloads_size = (ctx->payloads_size + 3) & ~3;
  uint16_t* head = (uint16_t*)(ctx->payloads + sym->payload_start);
  head[0] = 0; // Will become name length and flags.
  head[1] = 0; // no relocs
  head[2] = 0; // Will become payload length.
  head[3] = 0; // no cfi
  /*
    After head, payload is:
      uint8_t lib_len;
      uint8_t name_len;
      uint8_t ver_len;
      char lib[lib_len]; // NUL-terminated if present
      char name[name_len]; // NUL-terminated
      char ver[ver_len]; // NUL-terminated if present
  */
  uint8_t* dst = (uint8_t*)(head + 4);
  uint8_t* chars = dst + 3;
  const char* src_start = ctx->toks.text + t->start + 1;
  const char* src_end = ctx->toks.text + t->end - 1;
  const char* colon_colon = find_colon_colon(src_start, src_end);
  dst[0] = 0;
  if (colon_colon) {
    size_t n = colon_colon - src_start;
    dst[0] = n + 1;
    memcpy(chars, src_start, n);
    chars[n] = '\0';
    chars = chars + n + 1;
    src_start = colon_colon + 2;
  }
  const char* at = memchr(src_start, '@', src_end - src_start);
  {
    size_t n = (at ? at : src_end) - src_start;
    dst[1] = n + 1;
    memcpy(chars, src_start, n);
    chars[n] = '\0';
    chars = chars + n + 1;
  }
  dst[2] = 0;
  if (at) {
    size_t n = src_end - at;
    dst[2] = n;
    memcpy(chars, at + 1, n - 1);
    chars[n - 1] = '\0';
    chars = chars + n;
  }
  head[2] = chars - dst;
  ctx->payloads_size = chars - ctx->payloads;
}

static token_t* parse_extern(parse_ctx_t* ctx, asm_sym_t* sym, token_t* t) {
  uint32_t tt = (++t)->type;
  if (tt != TOK_PUNCT('(')) parse_err(t, "expected ( after linkname", ctx);
  tt = (++t)->type;
  if (tt != TOK_STR_LIT && tt != TOK_STR_LIT_ESC) parse_err(t, "expected string literal within linkname", ctx);
  encode_extern(ctx, sym, t);
  tt = (++t)->type;
  if (tt != TOK_PUNCT(')')) parse_err(t, "expected ) to complete linkname", ctx);
  tt = (++t)->type;
  if (tt != TOK_PUNCT(';')) parse_err(t, "expected ;", ctx);
  return ++t;
}

static void encode_impose_rename(parse_ctx_t* ctx, asm_sym_t* sym, token_t* t_old, token_t* t_new) {
  uint32_t n_old, n_new;
  const char* s_old = tokeniser_decode_str_lit(&ctx->toks, t_old, &n_old);
  const char* s_new = tokeniser_decode_str_lit(&ctx->toks, t_new, &n_new);
  (void)ensure_payloads_space(ctx, n_old + n_new + 16);
  sym->payload_start = ctx->payloads_size = (ctx->payloads_size + 3) & ~3;
  uint16_t* head = (uint16_t*)(ctx->payloads + sym->payload_start);
  head[0] = 0; // Will become name length and flags.
  head[1] = 0; // no relocs
  head[2] = 5 + n_old + n_new; // Payload length.
  head[3] = 0; // no cfi
  /*
    After head, payload is:
      uint8_t old_len;
      uint8_t zero; // == 0
      uint8_t new_len;
      char old[old_len]; // NUL-terminated
      char new[new_len]; // NUL-terminated
  */
  uint8_t* dst = (uint8_t*)(head + 4);
  dst[0] = n_old + 1;
  dst[1] = 0;
  dst[2] = n_new + 1;
  dst += 3;
  memcpy(dst, s_old, n_old);
  dst += n_old;
  *dst++ = '\0';
  memcpy(dst, s_new, n_new);
  dst += n_new;
  *dst++ = '\0';
  ctx->payloads_size = dst - ctx->payloads;
}

static token_t* parse_impose_rename(parse_ctx_t* ctx, asm_sym_t* sym, token_t* t) {
  uint32_t tt = (++t)->type;
  if (tt != TOK_PUNCT('(')) parse_err(t, "expected ( after impose_rename", ctx);
  tt = (++t)->type;
  if (tt != TOK_STR_LIT) parse_err(t, "expected string literal within impose_rename", ctx);
  token_t* t_old = t;
  tt = (++t)->type;
  if (tt != TOK_PUNCT(',')) parse_err(t, "expected , in middle of impose_rename", ctx);
  tt = (++t)->type;
  if (tt != TOK_STR_LIT) parse_err(t, "expected string literal within impose_rename", ctx);
  token_t* t_new = t;
  tt = (++t)->type;
  if (tt != TOK_PUNCT(')')) parse_err(t, "expected ) to complete impose_rename", ctx);
  tt = (++t)->type;
  if (tt != TOK_PUNCT(';')) parse_err(t, "expected ;", ctx);
  encode_impose_rename(ctx, sym, t_old, t_new);
  return ++t;
}

static void parse_file(parse_ctx_t* ctx) {
  token_t* t = ctx->toks.tokens;
  for (;;) {
    uint32_t flags = 0;
    uint32_t tt = t->type;
    if (tt == TOK_EOF) break;
    for (;;) {
      if (tt == KW_public) { flags |= ASM_SYM_FLAG_PUBLIC;
      } else if (tt == KW_extern) { flags |= ASM_SYM_FLAG_EXTERN;
      } else if (tt == KW_const) { flags |= ASM_SYM_FLAG_CONST;
      } else if (tt == KW_init) { flags |= ASM_SYM_FLAG_INIT;
      } else if (tt == KW_fini) { flags |= ASM_SYM_FLAG_FINI;
      } else {
        break;
      }
      tt = (++t)->type;
    }
    if ((flags & ASM_SYM_FLAG_PUBLIC) && (flags & ASM_SYM_FLAG_EXTERN)) {
      parse_err(t, "public cannot be applied to externs", ctx);
    }
    if (tt == KW_function) {
      flags |= ASM_SYM_FLAG_FUNCTION;
      if (flags & ASM_SYM_FLAG_CONST) {
        parse_err(t, "const cannot be applied to functions", ctx);
      }
    } else if (tt == KW_variable) {
      if (flags & (ASM_SYM_FLAG_INIT | ASM_SYM_FLAG_FINI)) {
        parse_err(t, "init/fini cannot be applied to variables", ctx);
      }
    } else {
      parse_err(t, "expected function or variable keyword", ctx);
    }
    tt = (++t)->type;
    if (tt >= TOK_IDENT_THR) {
      parse_err(t, "expected identifier", ctx);
    }
    uint32_t sym_index = ctx->syms_size++;
    if (sym_index == ctx->syms_capacity) {
      ctx->syms_capacity *= 2;
      ctx->syms = realloc(ctx->syms, sizeof(*ctx->syms) * ctx->syms_capacity);
    }
    if (uuht_lookup_or_set(&ctx->sym_names, tt, sym_index) != sym_index) {
      parse_err(t, "name already defined", ctx);
    }
    asm_sym_t* sym = ctx->syms + sym_index;
    sym->name = tt;
    sym->flags = flags;
    tt = (++t)->type;
    if (flags & ASM_SYM_FLAG_EXTERN) {
      if (tt == KW_linkname) {
        t = parse_extern(ctx, sym, t);
      } else if (tt == KW_impose_rename) {
        t = parse_impose_rename(ctx, sym, t);
      } else {
        parse_err(t, "expected linkname keyword", ctx);
      }
    } else {
      if (tt != TOK_PUNCT('{')) parse_err(t, "expected {", ctx);
      ++t;
      sym->payload_start = ctx->payloads_size;
      if (flags & ASM_SYM_FLAG_FUNCTION) {
        ctx->cfi.cfa_reg = CFI_REG_RSP;
        ctx->cfi.n_push = 1;
        ctx->cfi.saved_mask = 0;
        while (t->type != TOK_PUNCT('}')) {
          t = parse_insn(t, ctx);
        }
      } else {
        while (t->type != TOK_PUNCT('}')) {
          t = parse_data(t, ctx);
        }
      }
      ++t;
    }
    sym->payload_end = ctx->payloads_size;
  }
}

static bool is_zero_mem(const uint8_t* mem, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (mem[i]) {
      return false;
    }
  }
  return true;
}

static void resolve_labels_for_data(parse_ctx_t* ctx, asm_sym_t* sym) {
  const uint8_t* payload_start = ctx->payloads + sym->payload_start;
  const uint8_t* payload_end = ctx->payloads + sym->payload_end;
  uint32_t size = 0;
  uint32_t align = 1;
  for (const uint8_t* itr = payload_start; itr < payload_end; ) {
    uint8_t head = *itr++;
    size += head;
    itr += head;
    if (head > align && !(head & (head - 1))) {
      align = head;
    }
  }

  ensure_payloads_space(ctx, size + 12);
  ctx->payloads_size = (ctx->payloads_size + 3u) & ~3u;
  uint16_t* head_out = (uint16_t*)(ctx->payloads + ctx->payloads_size);
  head_out[0] = 0; // Will become name length and flags.
  head_out[1] = 0; // No relocs.
  head_out[2] = size;
  head_out[3] = align << 8; // Will gain allocation category in low byte.
  uint8_t* out = (uint8_t*)(head_out + 4);
  payload_start = ctx->payloads + sym->payload_start;
  payload_end = ctx->payloads + sym->payload_end;
  for (const uint8_t* itr = payload_start; itr < payload_end; ) {
    uint8_t head = *itr++;
    memcpy(out, itr, head);
    itr += head;
    out += head;
  }
  enum erw_alloc_category cat;
  if (sym->flags & ASM_SYM_FLAG_CONST) {
    cat = erw_alloc_category_v_r;
  } else if (is_zero_mem(out - size, size)) {
    cat = erw_alloc_category_v_rw_zero;
    out -= size;
  } else {
    cat = erw_alloc_category_v_rw;
  }
  head_out[3] |= cat;
  sym->payload_start = ctx->payloads_size;
  sym->payload_end = ctx->payloads_size = out - ctx->payloads;
}

static uint32_t max_u32(uint32_t* labels, uint32_t n) {
  uint32_t result = 0;
  for (uint32_t i = 0; i < n; ++i) {
    if (labels[i] > result) {
      result = labels[i];
    }
  }
  return result;
}

static bool remove_labels_low_bit(uint32_t* labels, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) {
    if (labels[i] & 1) {
      return false;
    }
    labels[i] >>= 1;
  }
  return true;
}

typedef struct resolve_state_t {
  uuht_t local_labels; // sht index to reloc index
  uint32_t* positions;
  uint32_t positions_capacity;
} resolve_state_t;

static uint8_t* emit_cfi_advance(uint8_t* dst, uint32_t by) {
  if (by <= 0x3f) {
    *dst++ = 0x40 + by;
  } else if (by < 0xff) {
    *dst++ = 0x02;
    *dst++ = by;
  } else if (by < 0xffff) {
    uint16_t u16 = by;
    *dst = 0x03;
    memcpy(dst + 1, &u16, sizeof(u16));
    dst += 1 + sizeof(u16);
  } else {
    *dst = 0x04;
    memcpy(dst + 1, &by, sizeof(by));
    dst += 1 + sizeof(by);
  }
  return dst;
}

static void resolve_labels_for_func(parse_ctx_t* ctx, asm_sym_t* sym, resolve_state_t* rs) {
  uuht_clear(&rs->local_labels);
  uuht_set(&rs->local_labels, sym->name, 0);

  // First scan to identify locals and number of relocs.
  const uint8_t* payload_start = ctx->payloads + sym->payload_start;
  const uint8_t* payload_end = ctx->payloads + sym->payload_end;
  uint32_t reloc_idx = 0;
  uint32_t cfi_size_bound = 0;
  for (const uint8_t* itr = payload_start; itr < payload_end; ) {
    uint8_t head = *itr++;
    if (head < CMD_CFI) {
      itr += head;
    } else if (head < CMD_reloc) {
      head -= CMD_CFI;
      cfi_size_bound = cfi_size_bound + 5 + head;
      itr = itr + head;
    } else {
      ++reloc_idx;
      if (head == CMD_reloc + RELOC_def_label) {
        uint32_t name;
        memcpy(&name, itr - 1 - sizeof(name), sizeof(name));
        if (uuht_lookup_or_set(&rs->local_labels, name, reloc_idx) != reloc_idx) {
          FATAL("%s defines local label %s multiple times", sht_u_key(&ctx->toks.idents, sym->name), sht_u_key(&ctx->toks.idents, name));
        }
      }
    }
  }
  if (reloc_idx >= rs->positions_capacity) {
    rs->positions_capacity = reloc_idx + 1;
    free(rs->positions);
    rs->positions = malloc(rs->positions_capacity * sizeof(rs->positions[0]));
  }

  // Positions move apart, until convergence is reached.
  memset(rs->positions, 0, (reloc_idx + 1ull) * sizeof(rs->positions[0]));
  uint32_t off;
  uint32_t unstable;
  uint32_t n_runtime_reloc;
  do {
    const uint8_t* code_end = NULL;
    off = 0;
    unstable = 0;
    reloc_idx = 0;
    n_runtime_reloc = 0;
    for (const uint8_t* itr = payload_start; itr < payload_end; ) {
      uint8_t head = *itr++;
      if (head < CMD_CFI) {
        itr += head;
        off += head;
        code_end = itr;
      } else if (head < CMD_reloc) {
        itr = itr + head - CMD_CFI;
      } else {
        uint32_t reloc_val;
        uint64_t k;
        ++reloc_idx;
        switch (head) {
        case CMD_reloc + 3: code_end -= 2; // fallthrough
        case CMD_reloc + 2: --code_end; // fallthrough
        case CMD_reloc + 1: --code_end; // fallthrough
        case CMD_reloc + 0:
          memcpy(&reloc_val, code_end - sizeof(reloc_val), sizeof(reloc_val));
          if (uuht_contains(&rs->local_labels, reloc_val)) {
            // ok
          } else if ((k = uuht_contains(&ctx->sym_names, reloc_val))) {
            ++n_runtime_reloc;
          } else {
            goto undef_reloc;
          }
          break;
        case CMD_reloc + RELOC_call:
        case CMD_reloc + RELOC_jmp:
        case CMD_reloc + RELOC_jcc:
          memcpy(&reloc_val, code_end - sizeof(reloc_val), sizeof(reloc_val));
          off -= sizeof(uint32_t);
          if ((k = uuht_contains(&rs->local_labels, reloc_val))) {
            if (head != CMD_reloc + RELOC_call) {
              k >>= 32;
              k = rs->positions[k] - (k >= reloc_idx ? rs->positions[reloc_idx] : (off + 1));
              if ((int8_t)(uint8_t)k == (int32_t)(uint32_t)k) {
                off += 1;
                break;
              }
            }
          } else if ((k = uuht_contains(&ctx->sym_names, reloc_val))) {
            ++n_runtime_reloc;
            if (ctx->syms[k >> 32].flags & ASM_SYM_FLAG_EXTERN) {
              off += 1;
              if (head == CMD_reloc + RELOC_jcc) {
                FATAL("%s contains bad reference to extern label %s", sht_u_key(&ctx->toks.idents, sym->name), sht_u_key(&ctx->toks.idents, reloc_val));
              }
            }
          } else { undef_reloc:
            FATAL("%s contains reference to undefined label %s", sht_u_key(&ctx->toks.idents, sym->name), sht_u_key(&ctx->toks.idents, reloc_val));
          }
          off += 4 + (head == CMD_reloc + RELOC_jcc);
          break;
        case CMD_reloc + RELOC_def_label:
          off -= sizeof(uint32_t);
          break;
        case CMD_reloc + RELOC_align: {
          uint8_t amt = code_end[-1];
          off = (off - 1 + amt) & ~(uint32_t)amt;
          break; }
        case CMD_reloc + RELOC_u8_label_lut:
          head = *itr++;
          uint32_t dst_reg = itr[0];
          itr += head;
          off += 30;
          if (dst_reg >= 4) ++off;
          if ((dst_reg & 7) == 5) ++off;
          break;
        case CMD_reloc + RELOC_phantom_ref:
          memcpy(&reloc_val, code_end - sizeof(reloc_val), sizeof(reloc_val));
          off -= sizeof(uint32_t);
          if (uuht_contains(&ctx->sym_names, reloc_val)) {
            ++n_runtime_reloc;
          } else {
            goto undef_reloc;
          }
          break;
        default:
          FATAL("bad reloc cmd");
        }
        if (off < rs->positions[reloc_idx]) {
          FATAL("logic error; function %s reloc %u moved left from %u to %u", sht_u_key(&ctx->toks.idents, sym->name), reloc_idx, rs->positions[reloc_idx], off);
        }
        
        unstable |= off ^ rs->positions[reloc_idx];
        rs->positions[reloc_idx] = off;
      }
    }
  } while (unstable);

  // Positions have stopped moving; can actually emit code now.
  ensure_payloads_space(ctx, off + 16 + cfi_size_bound + n_runtime_reloc * sizeof(uint32_t));
  ctx->payloads_size = (ctx->payloads_size + 3u) & ~3u;
  uint16_t* head_out = (uint16_t*)(ctx->payloads + ctx->payloads_size);
  head_out[0] = 0; // Will become name length and flags.
  head_out[1] = n_runtime_reloc;
  head_out[2] = off;
  head_out[3] = 0; // Will become size of CFI data.
  uint32_t* reloc_out = (uint32_t*)(head_out + 4);
  uint8_t* out = (uint8_t*)(reloc_out + n_runtime_reloc);
  uint8_t* cfi_out = out + off;
  uint8_t* cfi_out_initial = cfi_out;
  uint32_t last_cfi_off = 0;
  payload_start = ctx->payloads + sym->payload_start;
  payload_end = ctx->payloads + sym->payload_end;
  off = 0;
  reloc_idx = 0;
  for (const uint8_t* itr = payload_start; itr < payload_end; ) {
    uint8_t head = *itr++;
    if (head < CMD_CFI) {
      memcpy(out + off, itr, head);
      itr += head;
      off += head;
    } else if (head < CMD_reloc) {
      if (last_cfi_off != off) {
        cfi_out = emit_cfi_advance(cfi_out, off - last_cfi_off);
        last_cfi_off = off;
      }
      head -= CMD_CFI;
      memcpy(cfi_out, itr, head);
      itr += head;
      cfi_out += head;
    } else {
      uint32_t reloc_off = off - sizeof(uint32_t);
      uint32_t reloc_val;
      uint64_t k;
      ++reloc_idx;
      switch (head) {
      case CMD_reloc + 3: reloc_off -= 2; // fallthrough
      case CMD_reloc + 2: --reloc_off; // fallthrough
      case CMD_reloc + 1: --reloc_off; // fallthrough
      case CMD_reloc + 0:
        memcpy(&reloc_val, out + reloc_off, sizeof(reloc_val));
        if ((k = uuht_contains(&rs->local_labels, reloc_val))) {
          reloc_val = rs->positions[k >> 32] - off;
        } else {
          k = uuht_contains(&ctx->sym_names, reloc_val);
          reloc_val = k >> 32;
          *reloc_out++ = (reloc_off << 8) | (head & RUN_RELOC_IMM_SIZE_MASK);
        }
        memcpy(out + reloc_off, &reloc_val, sizeof(reloc_val));
        break;
      case CMD_reloc + RELOC_call:
      case CMD_reloc + RELOC_jmp:
      case CMD_reloc + RELOC_jcc:
        memcpy(&reloc_val, out + reloc_off, sizeof(reloc_val));
        off = reloc_off;
        if ((k = uuht_contains(&rs->local_labels, reloc_val))) {
          k = rs->positions[k >> 32] - off - 1;
          if (head == CMD_reloc + RELOC_call) {
            // no rel8 encoding possible
          } else if ((int8_t)(uint8_t)k == (int32_t)(uint32_t)k) {
            if (head == CMD_reloc + RELOC_jmp) {
              // e9 -> eb
              *(out + off - 1) = 0xeb;
            }
            out[off++] = (uint8_t)k;
            break;
          }
          reloc_val = k - 3 - (head == CMD_reloc + RELOC_jcc);
        } else {
          k = uuht_contains(&ctx->sym_names, reloc_val);
          reloc_val = k >> 32;
          if (ctx->syms[reloc_val].flags & ASM_SYM_FLAG_EXTERN) {
            // e8 -> ff /2, e9 -> ff /4
            uint8_t pre = *(out + off - 1);
            *(out + off - 1) = 0xff;
            out[off++] = ((pre & 1) << 4) + 0x15;
          }
          if (head == CMD_reloc + RELOC_jcc) {
            *reloc_out++ = (off + 1) << 8;
          } else {
            *reloc_out++ = (off << 8) | RUN_RELOC_CALL_OR_JMP;
          }
        }
        if (head == CMD_reloc + RELOC_jcc) {
          // 7x -> 0f 8x
          uint8_t pre = *(out + off - 1);
          *(out + off - 1) = 0x0f;
          out[off++] = pre + 0x10;
        }
        memcpy(out + off, &reloc_val, sizeof(reloc_val));
        off += 4;
        break;
      case CMD_reloc + RELOC_phantom_ref:
        memcpy(&reloc_val, out + reloc_off, sizeof(reloc_val));
        off = reloc_off;
        k = uuht_contains(&ctx->sym_names, reloc_val);
        reloc_val = k >> 32;
        *reloc_out++ = (reloc_val << 8) | RUN_RELOC_PHANTOM;
        break;
      case CMD_reloc + RELOC_def_label:
        off = reloc_off;
        break;
      case CMD_reloc + RELOC_align: {
        uint32_t amt = out[--off];
        amt = (amt + 1 - off) & amt;
        if (amt > 9) {
          // Can't fill with a single nop instruction, so jmp over the gap.
          if (amt <= 129) {
            out[off++] = 0xeb;
            amt -= 2;
            out[off++] = amt;
          } else {
            out[off++] = 0xe9;
            amt -= 5;
            memcpy(out + off, &amt, 4);
            off += 4;
          }
        }
        if (amt <= 9) {
          // Fill with a single nop instruction
          const char* nops = "\x90" "\x66\x90" "\x0f\x1f\x00" "\x0f\x1f\x40\x00" "\x0f\x1f\x44\x00\x00" "\x66\x0f\x1f\x44\x00\x00" "\x0f\x1f\x80\x00\x00\x00\x00" "\x0f\x1f\x84\x00\x00\x00\x00\x00" "\x66\x0f\x1f\x84\x00\x00\x00\x00\x00";
          memcpy(out + off, nops + (amt * (amt - 1)) / 2, amt);
        } else {
          // Fill with int3 instructions
          memset(out + off, 0xcc, amt);
        }
        off += amt;
        break; }
      case CMD_reloc + RELOC_u8_label_lut: {
        uint32_t labels[8];
        head = *itr++;
        uint32_t dst_reg = itr[0];
        uint32_t n_label = (head - 1) / sizeof(uint32_t);
        memcpy(labels, itr + 1, n_label * sizeof(uint32_t));
        itr += head;
        for (uint32_t i = 0; i < n_label; ++i) {
          if (!(k = uuht_contains(&rs->local_labels, labels[i]))) {
            FATAL("u8_label_lut in %s refers to undefined label %s", sht_u_key(&ctx->toks.idents, sym->name), sht_u_key(&ctx->toks.idents, labels[i]));
          }
          labels[i] = rs->positions[k >> 32];
        }
        // Identify the minimum, subtract it off
        uint32_t min_label = ~(uint32_t)0;
        for (uint32_t i = 0; i < n_label; ++i) {
          if (labels[i] < min_label) min_label = labels[i];
        }
        for (uint32_t i = 0; i < n_label; ++i) {
          labels[i] -= min_label;
        }
        // Pack each delta down to at most 8 bits
        uint32_t addr_shift = 0;
        uint32_t max_val;
        for (; (max_val = max_u32(labels, n_label)) > 255; ++addr_shift) {
          if (addr_shift >= 3 || !remove_labels_low_bit(labels, n_label)) {
            FATAL("u8_label_lut in %s cannot encode labels in 8 bits (largest value is %u)", sht_u_key(&ctx->toks.idents, sym->name), (unsigned)(max_val << addr_shift));
          }
        }
        // shl ecx, 3 (3 bytes)
        out[off++] = 0xc1;
        out[off++] = 0xe1;
        out[off++] = 0x03;
        // mov dst_reg, imm64 (10 bytes)
        out[off++] = 0x48 + ((dst_reg & 8) ? 1 : 0);
        out[off++] = 0xb8 + (dst_reg & 7);
        for (uint32_t i = 0; i < 8; ++i) {
          out[off++] = (i < n_label) ? labels[i] : 0;
        }
        // shr dst_reg, rcx (3 bytes)
        out[off++] = 0x48 + ((dst_reg & 8) ? 1 : 0);
        out[off++] = 0xd3;
        out[off++] = 0xe8 + (dst_reg & 7);
        // movzx ecx, dst_reg.u8 (3 or 4 bytes)
        if (dst_reg >= 4) {
          out[off++] = 0x40 + ((dst_reg & 8) ? 1 : 0);
        }
        out[off++] = 0x0f;
        out[off++] = 0xb6;
        out[off++] = 0xc8 + (dst_reg & 7);
        // lea dst_reg, min_label (7 bytes)
        out[off++] = 0x48 + ((dst_reg & 8) ? 4 : 0);
        out[off++] = 0x8d;
        out[off++] = 0x05 + ((dst_reg & 7) * 8);
        off += 4;
        min_label -= off;
        memcpy(out + off - sizeof(min_label), &min_label, sizeof(min_label));
        // lea dst_reg, [dst_reg + rcx << addr_shift] (4 or 5 bytes)
        out[off++] = 0x48 + ((dst_reg & 8) ? 5 : 0);
        out[off++] = 0x8d;
        out[off++] = 0x04 + ((dst_reg & 7) * 8) + ((dst_reg & 7) == 5 ? 0x40 : 0);
        out[off++] = 0x08 + (dst_reg & 7) + (0x40 * addr_shift);
        if ((dst_reg & 7) == 5) out[off++] = 0;
        break; }
      default:
        FATAL("bad reloc cmd");
      }
      if (rs->positions[reloc_idx] != off) {
        FATAL("inconsistent logic (function %s, reloc %u, expected %u, got %u)", sht_u_key(&ctx->toks.idents, sym->name), reloc_idx, rs->positions[reloc_idx], off);
      }
    }
  }
  head_out[3] = cfi_out - cfi_out_initial;
  sym->payload_start = ctx->payloads_size;
  sym->payload_end = ctx->payloads_size = cfi_out - ctx->payloads;
}

static void resolve_labels(parse_ctx_t* ctx) {
  resolve_state_t rs;
  uuht_init(&rs.local_labels);
  rs.positions = NULL;
  rs.positions_capacity = 0;
  for (asm_sym_t *sym = ctx->syms, *end = sym + ctx->syms_size; sym < end; ++sym) {
    uint32_t flags = sym->flags;
    if (flags & ASM_SYM_FLAG_EXTERN) continue;
    if (flags & ASM_SYM_FLAG_FUNCTION) {
      resolve_labels_for_func(ctx, sym, &rs);
    } else {
      resolve_labels_for_data(ctx, sym);
    }
  }
  free(rs.positions);
  uuht_free(&rs.local_labels);
}

static void emit_bytes(uint8_t* src, uint32_t n, FILE* f) {
  if (n) {
    uint32_t i = 0;
    for (;;) {
      fprintf(f, "0x%02x, ", src[i]);
      if (++i >= n) break;
      if (!(i & 127)) fprintf(f, "\n");
    }
    fprintf(f, "\n");
  }
}

static void rh_insert(uint64_t* table, uint32_t mask, uint32_t h, uint32_t v) {
  uint64_t hv = h | (((uint64_t)v) << 32);
  for (uint32_t d = 0;; ++d) {
    uint32_t idx = ((uint32_t)hv + d) & mask;
    uint64_t slot = table[idx];
    if (!slot) {
      table[idx] = hv;
      break;
    }
    uint32_t d2 = (idx - (uint32_t)slot) & mask;
    if (d2 < d) {
      table[idx] = hv;
      hv = slot;
      d = d2;
    }
  }
}

static void emit_final_blob(parse_ctx_t* ctx, FILE* f) {
  // Count various types of symbol.
  uint32_t hash_mask = 0;
  uint32_t num_extern = 0;
  for (asm_sym_t *sym = ctx->syms, *end = sym + ctx->syms_size; sym < end; ++sym) {
    uint32_t flags = sym->flags;
    if (flags & ASM_SYM_FLAG_PUBLIC) ++hash_mask;
    if (flags & ASM_SYM_FLAG_EXTERN) ++num_extern;
  }

  // Allocate a sufficiently-sized hash table.
  hash_mask += (hash_mask >> 2) + (hash_mask >> 3);
  hash_mask = (2u << (31 - __builtin_clz(hash_mask | 1))) - 1;
  uint64_t* hash_table = (uint64_t*)calloc(hash_mask + 1ull, sizeof(uint64_t));

  // Visit all externs last.
  uint32_t* visit_order = malloc(ctx->syms_size * sizeof(uint32_t));
  for (uint32_t i = 0, n = ctx->syms_size, out_r = 0, out_e = n - num_extern; i < n; ++i) {
    visit_order[ctx->syms[i].flags & ASM_SYM_FLAG_EXTERN ? out_e++ : out_r++] = i;
  }

  // Assign final positions to symbols
  uint32_t off = (hash_mask + 1) * 8ull;
  for (uint32_t i = 0, n = ctx->syms_size; i < n; ++i) {
    asm_sym_t* sym = ctx->syms + visit_order[i];
    uint32_t flags = sym->flags;
    const char* name_str = sht_u_key(&ctx->toks.idents, sym->name);
    if (flags & ASM_SYM_FLAG_PUBLIC) {
      uint32_t name_len = sht_u_key_length(&ctx->toks.idents, sym->name);
      *(ctx->payloads + sym->payload_start + 1) = name_len;
      off += name_len + 1;
      off = (off + 3u) &~ 3u;
      uint32_t hash = sht_hash_mem(name_str, name_len);
      rh_insert(hash_table, hash_mask, hash, off);
    }
    ctx->payloads[sym->payload_start] = flags; // Save flags in payload
    off = (off + 3u) &~ 3u;
    sym->flags = off; // Flags is now final offset
    if (has_prefix(name_str, "_polyfill_")) {
      fprintf(f, "#define token_for_%s %u\n", name_str, (unsigned)off);
    }
    off += (sym->payload_end - sym->payload_start);
  }

  // Fixup relocations to refer to final offsets
  for (asm_sym_t *sym = ctx->syms, *end = sym + ctx->syms_size; sym < end; ++sym) {
    uint16_t* head = (uint16_t*)(ctx->payloads + sym->payload_start);
    uint32_t n_reloc = head[1];
    if (n_reloc) {
      uint32_t* relocs = (uint32_t*)(head + 4);
      uint8_t* data = (uint8_t*)(relocs + n_reloc);
      for (uint32_t i = 0; i < n_reloc; ++i) {
        uint32_t info = relocs[i];
        if (info & RUN_RELOC_PHANTOM) {
          uint32_t val = info >> 8;
          val = ctx->syms[val].flags; // Not flags at this point
          relocs[i] = (info & 0xff) | (val << 8);
        } else {
          uint32_t where = info >> 8;
          uint32_t val;
          memcpy(&val, data + where, sizeof(val));
          val = ctx->syms[val].flags; // Not flags at this point
          memcpy(data + where, &val, sizeof(val));
        }
      }
    }
  }

  // Emit it
  fprintf(f, "#define polyfill_code_mask 0x%x\n", hash_mask);
  fprintf(f, "static const uint8_t polyfill_code[] __attribute__ ((aligned(8))) = {\n");
  emit_bytes((uint8_t*)hash_table, (hash_mask + 1ull) * sizeof(uint64_t), f);
  off = (hash_mask + 1) * 8ull;
  for (uint32_t i = 0, n = ctx->syms_size; i < n; ++i) {
    asm_sym_t* sym = ctx->syms + visit_order[i];
    uint32_t len = *(ctx->payloads + sym->payload_start + 1);
    len += (len != 0);
    off += len;
    if (off & 3) {
      uint32_t aligned = (off + 3u) &~ 3u;
      emit_bytes((uint8_t*)"\0\0", aligned - off, f);
      off = aligned;
    }
    if (len) {
      emit_bytes((uint8_t*)sht_u_key(&ctx->toks.idents, sym->name), len, f);
    }
    if (off != sym->flags) {
      FATAL("inconsistent logic");
    }
    len = sym->payload_end - sym->payload_start;
    emit_bytes(ctx->payloads + sym->payload_start, len, f);
    off += len;
  }

  free(visit_order);
  free(hash_table);
  fprintf(f, "0};\n");
}

int main(int argc, const char** argv) {
  if (argc < 3) {
    FATAL("Usage: %s INPUTS... OUTPUT\n", argv[0]);
  }
  FILE* out = fopen(argv[--argc], "w");
  if (!out) {
    FATAL("Could not open %s for writing", argv[argc]);
  }

  parse_ctx_t ctx;
  ctx.payloads_size = 0;
  ctx.payloads = malloc(ctx.payloads_capacity = 128);
  ctx.syms_size = 0;
  ctx.syms = malloc((ctx.syms_capacity = 8) * sizeof(*ctx.syms));
  tokeniser_init(&ctx.toks);
  uuht_init(&ctx.sym_names);
  enter_keywords(&ctx.toks.idents);
  enter_insns(&ctx.toks.idents);
  enter_operands(&ctx.toks.idents);

  for (int i = 1; i < argc; ++i) {
    tokeniser_load_file(&ctx.toks, argv[i]);
    tokeniser_run(&ctx.toks);
    parse_file(&ctx);
  }
  resolve_labels(&ctx);
  emit_final_blob(&ctx, out);

  fclose(out);
  tokeniser_free(&ctx.toks);
  uuht_free(&ctx.sym_names);
  free(ctx.payloads);
  free(ctx.syms);
  return 0;
}
