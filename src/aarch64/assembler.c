#include <string.h>
#include "assembler.h"
#include "../uuht.h"
#include "../tokenise.h"
#include "../common.h"
#include "../erw.h"

#define KWS(KW) \
  KW("\001", b) \
  KW("\003", lsl) \
  KW("\003", ldp) \
  KW("\003", ldr) \
  KW("\003", stp) \
  KW("\003", str) \
  KW("\003", add) \
  KW("\003", sub) \
  KW("\003", mov) \
  KW("\007", paciasp) \
  KW("\007", autiasp) \
  KW("\003", ret) \
  KW("\003", cfi) \
  KW("\004", byte) \
  KW("\005", dword) \
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
  KW("\023", relative_offset_i32) \
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
  uint8_t sp_adj16;
  uint8_t fp_adj16; // If 0, fp is not known, and CFA defined via sp.
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

typedef union operand_value_t {
  uint64_t imm;
  struct {
    uint32_t imm;
    uint32_t lsl;
  } imm_lsl;
  struct {
    uint32_t token;
    uint32_t info;
  } ident;
  struct {
    uint8_t index; // OPERAND_GPR_X/OPERAND_GPR_W/OPERAND_GPR_SP, then index in low 5 bits
    uint8_t transform; // gpr_transform_t
    uint8_t amount; // shift/rotate amount when transform != gpr_transform_none
  } gpr;
  struct {
    uint8_t index;
    uint8_t kind; // fpr_kind_t
    uint8_t lane_width; // shift amount, i.e. 0=B / 1=H / 2=S / 3=D / 4=Q 
    uint8_t lane_number; // when kind == fpr_kind_lane
  } fpr;
  struct {
    uint8_t base_reg; // ADDR_PRE/ADDR_POST/ADDR_REG_INDEX, then register index in low 5 bits
    union {
      int32_t offset; // When !ADDR_REG_INDEX
      struct { // When ADDR_REG_INDEX
        uint8_t index;
        uint8_t transform; // gpr_transform_t
        uint8_t amount; // shift amount when transform != gpr_transform_none
      } index_reg;
    };
  } addr;
} operand_value_t;

#define ADDR_PRE 0x20
#define ADDR_POST 0x40
#define ADDR_REG_INDEX 0x80

#define OPERAND_GPR_X  0x80
#define OPERAND_GPR_W  0x40
#define OPERAND_GPR_SP 0x20

#define INFERRED_Q_SET    0x20
#define INFERRED_SIZE_SET 0x10

typedef enum gpr_transform_t {
  gpr_transform_none = 0,
  // 1,2,3 unused
  // low two bits of these match instruction encoding:
  gpr_transform_lsl = 4,
  gpr_transform_lsr = 5,
  gpr_transform_asr = 6,
  gpr_transform_ror = 7,
  // low three bits of these match instruction encoding:
  gpr_transform_uxtb = 8,
  gpr_transform_uxth = 9,
  gpr_transform_uxtw = 10,
  gpr_transform_uxtx = 11,
  gpr_transform_sxtb = 12,
  gpr_transform_sxth = 13,
  gpr_transform_sxtw = 14,
  gpr_transform_sxtx = 15,
} gpr_transform_t;

typedef enum fpr_kind_t {
  fpr_kind_v8,
  fpr_kind_v16,
  fpr_kind_scalar,
  fpr_kind_lane,
} fpr_kind_t;

typedef enum operand_kind_t {
  operand_kind_gpr,
  operand_kind_fpr,
  operand_kind_imm,
  operand_kind_imm_lsl,
  operand_kind_ident,
  operand_kind_addr,
  operand_kind_addr_ident,
} operand_kind_t;

typedef enum ident_kind_t {
  ident_kind_none,
  ident_kind_insn,
  ident_kind_gpr,
  ident_kind_fpr,
  ident_kind_cc,
  ident_kind_lane_width,
} ident_kind_t;

typedef enum operand_class_t {
  operand_class_gpr, /* 4 bits for (R,W,X) x (plain,sp,sa,sl,xt), 2 bits for d/n/a/m */
  operand_class_fpr, /* 4 bits for operand_class_fpr_t, 2 bits for d/n/a/m */
  operand_class_imm, /* 6 bits for operand_class_imm_t */
  operand_class_other, /* 6 bits for operand_class_other_t */
} operand_class_t;

#define OPERAND_CLASS_GPR_R 10
#define OPERAND_CLASS_GPR_X 5
#define OPERAND_CLASS_GPR_W 0

#define OPERAND_CLASS_GPR_PLAIN 0
#define OPERAND_CLASS_GPR_SP 1
#define OPERAND_CLASS_GPR_SA 2
#define OPERAND_CLASS_GPR_SL 3
#define OPERAND_CLASS_GPR_XT 4

typedef enum operand_class_imm_t {
  operand_class_imm_k5,
  operand_class_imm_k12,
  operand_class_imm_k13,
  operand_class_imm_k16,
  operand_class_imm_k16l,
  operand_class_imm_kmov,
  operand_class_imm_ir,
  operand_class_imm_is,
  operand_class_imm_ilsl,
  operand_class_imm_ib40,
  operand_class_imm_inzcv,
  operand_class_imm_0,
} operand_class_imm_t;

typedef enum operand_class_fpr_t {
  operand_class_fpr_F,
  operand_class_fpr_FB,
  operand_class_fpr_VT,
  operand_class_fpr_VTW,
  operand_class_fpr_VD1,
  operand_class_fpr_VTidx_umov,
} operand_class_fpr_t;

#include "../../build/aarch64/assembler_gen.h"

typedef struct parsed_operands_t {
  operand_kind_t kind[4];
  uint8_t n;
  uint8_t inferred_size; // OPERAND_GPR_X, OPERAND_GPR_W, INFERRED_Q_SET, INFERRED_SIZE_SET
  bool need_reloc;
  uint32_t operand_bits;
  operand_value_t val[4];
} parsed_operands_t;

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

#define IDENT_INFO_KIND(info) ((ident_kind_t)((info) & 0xf))
#define IDENT_INFO_TRANSFORM(info) ((gpr_transform_t)(((info) & 0xf0) >> 4))
#define IDENT_INFO_PAYLOAD(info) ((info) >> 8)

static bool parse_v_arrangement(tokeniser_t* toks, token_t* t, operand_value_t* dst) {
  const char* src = toks->text + t->start;
  char c = *src++;
  if (c != '.') return false;
  uint64_t uval = 0;
  for (;;) {
    c = *src++;
    if ('0' <= c && c <= '9') {
      uval = uval * 10 + (c - '0');
    } else if (!(c == '\'' && '0' <= *src && *src <= '9')) {
      break;
    }
  }
  if (uval == 0 || uval > 512) return false;
  --src;
  uint32_t* infop = sht_lookup_p(&toks->idents, src, toks->text + t->end - src);
  if (!infop) return false;
  uint32_t info = *infop;
  if (IDENT_INFO_KIND(info) != ident_kind_lane_width) return false;
  info = IDENT_INFO_PAYLOAD(info);
  uval <<= info;
  if (uval != 8 && uval != 16) return false;
  dst->fpr.lane_width = info;
  dst->fpr.kind = (uval == 8 ? fpr_kind_v8 : fpr_kind_v16);
  return true;
}

static token_t* parse_operands(token_t* src, parsed_operands_t* dst, parse_ctx_t* ctx) {
  if (src->type == TOK_PUNCT(';')) {
    return ++src;
  }
  for (;;) {
    uint32_t tt = src->type;
    if (tt < TOK_IDENT_THR) {
      uint32_t info = *(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt);
      ++src;
      switch (IDENT_INFO_KIND(info)) {
      case ident_kind_gpr:
        dst->kind[dst->n] = operand_kind_gpr;
        dst->val[dst->n].gpr.index = IDENT_INFO_PAYLOAD(info);
        dst->val[dst->n].gpr.transform = gpr_transform_none;
        tt = src->type;
        if (tt < TOK_IDENT_THR) {
          info = *(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt);
          gpr_transform_t tr = IDENT_INFO_TRANSFORM(info);
          if (tr != gpr_transform_none) {
            tt = src[1].type;
            if (tt == TOK_PUNCT('#')) {
              uint64_t amount;
              src = parse_imm(src + 2, &amount, ctx);
              uint64_t limit = tr >= gpr_transform_uxtb ? 4 : dst->val[dst->n].gpr.index & OPERAND_GPR_X ? 63 : 31;
              if (amount > limit) parse_err(src - 1, "shift/extend amount too big", ctx);
              dst->val[dst->n].gpr.transform = tr;
              dst->val[dst->n].gpr.amount = amount;
            } else if (tr >= gpr_transform_uxtb) {
              if (tt < TOK_IDENT_THR) {
                info = *(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt);
                if (IDENT_INFO_KIND(info) == ident_kind_gpr) {
                  dst->n++;
                  break;
                }
              }
              dst->val[dst->n].gpr.transform = tr;
              dst->val[dst->n].gpr.amount = 0;
              ++src;
            }
          }
        }
        dst->n++;
        break;
      case ident_kind_fpr:
        dst->kind[dst->n] = operand_kind_fpr;
        info = IDENT_INFO_PAYLOAD(info);
        dst->val[dst->n].fpr.index = info & 0x1f;
        dst->val[dst->n].fpr.kind = fpr_kind_scalar;
        dst->val[dst->n].fpr.lane_width = (info >> 5) & 7;
        dst->val[dst->n].fpr.lane_number = 0;
        if (info >> 8) {
          if (src->type == TOK_NUMBER && parse_v_arrangement(&ctx->toks, src, &dst->val[dst->n])) {
            ++src;
          } else if (src->type == TOK_PUNCT('.') && (tt = src[1].type) < TOK_IDENT_THR && src[2].type == TOK_PUNCT('[') && src[3].type == TOK_NUMBER && src[4].type == TOK_PUNCT(']')) {
            info = *(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt);
            uint64_t k;
            if (IDENT_INFO_KIND(info) == ident_kind_lane_width && tokeniser_decode_uint(&ctx->toks, src+3, &k) && k <= 64) {
              info = IDENT_INFO_PAYLOAD(info);
              if (k < (16u >> info)) {
                src += 5;
                dst->val[dst->n].fpr.kind = fpr_kind_lane;
                dst->val[dst->n].fpr.lane_width = info;
                dst->val[dst->n].fpr.lane_number = k;
              }
            }
          }
        }
        dst->n++;
        break;
      default:
        if (dst->n == 0 && (IDENT_INFO_KIND(info) == ident_kind_insn || src->type == TOK_PUNCT(':'))) {
          return src-1;
        }
        dst->kind[dst->n] = operand_kind_ident;
        dst->val[dst->n].ident.token = tt;
        dst->val[dst->n].ident.info = info;
        dst->n++;
        break;
      }
    } else if (tt == TOK_PUNCT('[')) {
      tt = (++src)->type;
      if (tt >= TOK_IDENT_THR) parse_err(src, "expected identifier after [", ctx);
      uint32_t info = *(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt);
      if (IDENT_INFO_KIND(info) == ident_kind_gpr) {
        info = IDENT_INFO_PAYLOAD(info);
        if ((info & 31) == 31 && !(info & OPERAND_GPR_SP)) parse_err(src, "zero register cannot be used as base address", ctx);
        if (!(info & OPERAND_GPR_X)) parse_err(src, "32-bit register cannot be used as base address", ctx);
        dst->kind[dst->n] = operand_kind_addr;
        dst->val[dst->n].addr.base_reg = info & 31;
        dst->val[dst->n].addr.offset = 0;
        tt = (++src)->type;
        if (tt == TOK_PUNCT(',') || tt == TOK_PUNCT('+')) {
          tt = (++src)->type;
          if (tt < TOK_IDENT_THR) {
            info = *(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt);
            if (IDENT_INFO_KIND(info) != ident_kind_gpr) parse_err(src, "expected index register name", ctx);
            info = IDENT_INFO_PAYLOAD(info);
            if ((info & 31) == 31) parse_err(src, "invalid index register", ctx);
            dst->val[dst->n].addr.base_reg |= ADDR_REG_INDEX;
            dst->val[dst->n].addr.index_reg.index = info;
            dst->val[dst->n].addr.index_reg.amount = 0;
            tt = (++src)->type;
            if (tt < TOK_IDENT_THR) {
              info = *(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt);
              gpr_transform_t t = IDENT_INFO_TRANSFORM(info);
              if (t == gpr_transform_lsl || t == gpr_transform_uxtx || t == gpr_transform_sxtx) {
                t = gpr_transform_uxtx;
                if (!(dst->val[dst->n].addr.index_reg.index & OPERAND_GPR_X)) parse_err(src, "extend operator not valid for 32-bit register (use sxtw or uxtw)", ctx);
              } else if (t != gpr_transform_uxtw && t != gpr_transform_sxtw) {
                parse_err(src, "expected lsl/uxtx/sxtx/uxtw/sxtw", ctx);
              }
              dst->val[dst->n].addr.index_reg.transform = t;
              tt = (++src)->type;
              if (tt == TOK_PUNCT('#')) {
                uint64_t imm;
                src = parse_imm(src + 1, &imm, ctx);
                if (imm > 8) parse_err(src - 1, "shift amount too large", ctx);
                dst->val[dst->n].addr.index_reg.amount = (uint8_t)imm;
                tt = src->type;
              }
            } else if (info & OPERAND_GPR_X) {
              dst->val[dst->n].addr.index_reg.transform = gpr_transform_uxtx;
            } else {
              parse_err(src - 1, "32-bit index register must be followed by uxtw or sxtw", ctx);
            }
          } else {
            if (tt == TOK_PUNCT('#')) ++src;
            uint64_t imm;
            src = parse_imm(src, &imm, ctx);
            if ((int64_t)imm != (int32_t)(uint32_t)imm) parse_err(src - 1, "memory offset too large", ctx);
            dst->val[dst->n].addr.offset = (int32_t)(uint32_t)imm;
            tt = src->type;
          }
        }
        if (tt != TOK_PUNCT(']')) parse_err(src, "expected ] after identifier", ctx);
        tt = (++src)->type;
        if (tt == TOK_PUNCT('!')) {
          dst->val[dst->n].addr.base_reg |= ADDR_PRE;
          ++src;
        } else if (tt == TOK_PUNCT(',') && !(dst->val[dst->n].addr.base_reg & ADDR_REG_INDEX) && !dst->val[dst->n].addr.offset) {
          tt = (++src)->type;
          if (tt != TOK_PUNCT('#')) parse_err(src, "expected # as part of address post-index", ctx);
          uint64_t imm;
          src = parse_imm(src + 1, &imm, ctx);
          if ((int64_t)imm != (int32_t)(uint32_t)imm) parse_err(src - 1, "post-index offset too large", ctx);
          dst->val[dst->n].addr.base_reg |= ADDR_POST;
          dst->val[dst->n].addr.offset = (int32_t)(uint32_t)imm;
        }
        dst->n++;
      } else {
        dst->kind[dst->n] = operand_kind_addr_ident;
        dst->val[dst->n].ident.token = tt;
        dst->val[dst->n].ident.info = info;
        dst->n++;
        tt = (++src)->type;
        if (tt != TOK_PUNCT(']')) parse_err(src, "expected ] after identifier", ctx);
        ++src;
      }
    } else if (tt == TOK_PUNCT('#')) {
      dst->kind[dst->n] = operand_kind_imm;
      src = parse_imm(src + 1, &dst->val[dst->n].imm, ctx);
      if (src->type == KW_lsl && src[1].type == TOK_PUNCT('#')) {
        uint64_t amount;
        src = parse_imm(src + 2, &amount, ctx);
        uint64_t k = dst->val[dst->n].imm;
        if (k != (uint32_t)k || amount > 63) parse_err(src - 1, "invalid imm/lsl combination", ctx);
        dst->val[dst->n].imm_lsl.imm = (uint32_t)k;
        dst->val[dst->n].imm_lsl.lsl = (uint32_t)amount;
        dst->kind[dst->n] = operand_kind_imm_lsl;
      }
      dst->n++;
    } else if (tt == TOK_PUNCT('.') && dst->n == 0 && src[-1].type == KW_b) {
      tt = src[1].type;
      if (tt < TOK_IDENT_THR) {
        uint32_t info = *(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt);
        if (IDENT_INFO_KIND(info) == ident_kind_cc) {
          dst->kind[0] = operand_kind_ident;
          dst->val[0].ident.token = tt;
          dst->val[0].ident.info = info;
          dst->n = 1;
          src += 2;
          continue;
        }
      }
      break;
    } else {
      if (dst->n == 0) {
        break;
      }
      parse_err(src, "invalid instruction operand", ctx);
    }
    if (src->type != TOK_PUNCT(',')) {
      break;
    }
    if (dst->n == sizeof(dst->kind)/sizeof(dst->kind[0])) {
      parse_err(src, "instruction has too many operands", ctx);
    }
    ++src;
  }
  return src;
}

#define CMD_CFI 0x80
#define CMD_reloc 0xC0
// 0-31 are for operand_class_other_t values
#define RELOC_def_label 32
#define RELOC_align 33
#define RELOC_u8_label_lut 34

static bool is_non_vol_reg(uint8_t reg) {
  if (reg & OPERAND_GPR_SP) return true;
  reg &= 31;
  return 18 <= reg && reg <= 30;
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
#define DW_CFA_GNU_window_save  0x2d
#define DW_CFA_offset           0x80
#define DW_CFA_restore          0xc0

static uint8_t* cfi_sp_adjust(uint8_t* out, parse_ctx_t* ctx, int32_t delta, token_t* where) {
  if (delta != 0) {
    if (delta & 15) {
      parse_err(where, ".cfi arithmetic on sp does not maintain 16-byte alignment", ctx);
    }
    delta >>= 4;
    int32_t new_val = ctx->cfi.sp_adj16 - delta;
    if (delta < 0) {
      if (new_val > 255) parse_err(where, ".cfi stack overflow", ctx);
    } else {
      if (new_val < 0) parse_err(where, ".cfi stack underflow", ctx);
    }
    ctx->cfi.sp_adj16 = (uint8_t)new_val;
    if (ctx->cfi.fp_adj16 == 0) {
      out = write_cfi_op_uleb(out, DW_CFA_def_cfa_offset, new_val * 16);
    }
  }
  return out;
}

static void encode_insn_cfi(parsed_operands_t* operands, parse_ctx_t* ctx, token_t* where) {
  uint8_t* out = ensure_payloads_space(ctx, 15);
  uint32_t tt = where[-2].type;
  uint32_t nreg = 1;
  switch (tt) {
  case KW_stp:
    ++nreg;
    // fallthrough
  case KW_str: {
    uint8_t base = operands->val[nreg].addr.base_reg;
    int32_t offset = operands->val[nreg].addr.offset;
    if ((base & 31) != 31) parse_err(where, "stp.cfi should use sp as base register", ctx);
    if (base & ADDR_PRE) {
      out = cfi_sp_adjust(out, ctx, offset, where);
      offset = 0;
    }
    for (uint32_t i = 0; i < nreg; ++i) {
      uint8_t reg = operands->val[i].gpr.index;
      if (is_non_vol_reg(reg)) {
        if (!(reg & OPERAND_GPR_X)) parse_err(where, "stp.cfi should use 64-bit registers", ctx);
        if ((reg & 31) == 30 && !(ctx->cfi.saved_mask & 1)) parse_err(where, "paciasp.cfi should be used prior to saving lr", ctx);
        reg &= 0xf;
        if (!(ctx->cfi.saved_mask & (1u << reg))) {
          ctx->cfi.saved_mask |= (1u << reg);
          if (offset & 7) parse_err(where, "stp.cfi uses suspicious offset", ctx);
          out = write_cfi_op_uleb(out, DW_CFA_offset + 16 + reg, ctx->cfi.sp_adj16 * 2 - i + offset / -8);
        }
      }
    }
    if (base & ADDR_POST) {
      out = cfi_sp_adjust(out, ctx, offset, where);
    }
    ctx->payloads_size = out - ctx->payloads;
    return; }
  case KW_ldp:
    ++nreg;
    // fallthrough
  case KW_ldr: {
    uint8_t base = operands->val[nreg].addr.base_reg;
    int32_t offset = operands->val[nreg].addr.offset;
    if ((base & 31) != 31) parse_err(where, "ldp.cfi should use sp as base register", ctx);
    if (base & ADDR_PRE) {
      out = cfi_sp_adjust(out, ctx, offset, where);
      offset = 0;
    }
    bool restored_fp = false;
    for (uint32_t i = 0; i < nreg; ++i) {
      uint8_t reg = operands->val[i].gpr.index;
      if (is_non_vol_reg(reg)) {
        if (!(reg & OPERAND_GPR_X)) parse_err(where, "ldp.cfi should use 64-bit registers", ctx);
        if ((reg & 31) == 29) {
          restored_fp = true;
        }
        reg &= 0xf;
        if (ctx->cfi.saved_mask & (1u << reg)) {
          ctx->cfi.saved_mask -= (1u << reg);
          *out++ = CMD_CFI + 1;
          *out++ = DW_CFA_restore + 16 + reg;
        }
      }
    }
    if (base & ADDR_POST) {
      out = cfi_sp_adjust(out, ctx, offset, where);
    }
    if (restored_fp && ctx->cfi.fp_adj16 != 0) {
      ctx->cfi.fp_adj16 = 0;
      *out++ = CMD_CFI + 1;
      *out++ = DW_CFA_def_cfa;
      out = write_cfi_op_uleb(out, 31, ctx->cfi.sp_adj16 * 16);
    }
    ctx->payloads_size = out - ctx->payloads;
    return; }
  case KW_add:
  case KW_sub:
  case KW_mov: {
    int32_t offset;
    if (tt == KW_mov) {
      offset = 0;
    } else {
      if (operands->kind[2] != operand_kind_imm) break;
      offset = (int32_t)(uint32_t)operands->val[2].imm;
      if (tt == KW_sub) offset = -offset;
    }
    uint8_t dst = operands->val[0].gpr.index;
    uint8_t src = operands->val[1].gpr.index;
    if (!(dst & src & OPERAND_GPR_X)) parse_err(where, "add/sub/mov.cfi should use 64-bit registers", ctx);
    dst &= 31;
    src &= 31;
    if (dst == 31 && src == 31) {
      out = cfi_sp_adjust(out, ctx, offset, where);
    } else if (dst == 29 && src == 31) {
      uint8_t saved_sp = ctx->cfi.sp_adj16;
      (void)cfi_sp_adjust(out, ctx, offset, where);
      if (ctx->cfi.sp_adj16 == 0) parse_err(where, "cannot set fp to original value of sp", ctx);
      ctx->cfi.fp_adj16 = ctx->cfi.sp_adj16;
      ctx->cfi.sp_adj16 = saved_sp;
      if (offset) {
        *out++ = CMD_CFI + 1;
        *out++ = DW_CFA_def_cfa;
        out = write_cfi_op_uleb(out, dst, ctx->cfi.fp_adj16 * 16);
      } else {
        out = write_cfi_op_uleb(out, DW_CFA_def_cfa_register, dst);
      }
    } else if (dst == 31 && src == 29) {
      if (ctx->cfi.fp_adj16 == 0) parse_err(where, "cannot restore sp from unknown fp", ctx);
      ctx->cfi.sp_adj16 = ctx->cfi.fp_adj16;
      (void)cfi_sp_adjust(out, ctx, offset, where);
    } else {
      break;
    }
    ctx->payloads_size = out - ctx->payloads;
    return; }
  case KW_paciasp:
    if (ctx->cfi.sp_adj16 != 0) parse_err(where, "paciasp should be used with original sp", ctx);
    if (ctx->cfi.saved_mask & (1u << 30)) parse_err(where, "paciasp should be used before lr is saved", ctx);
    if (ctx->cfi.saved_mask & 1) parse_err(where, "lr already signed", ctx);
    ctx->cfi.saved_mask |= 1;
    *out++ = CMD_CFI + 1;
    *out++ = DW_CFA_GNU_window_save;
    ctx->payloads_size = out - ctx->payloads;
    return;
  case KW_autiasp:
    if (ctx->cfi.sp_adj16 != 0) parse_err(where, "autiasp should be used with original sp", ctx);
    if (ctx->cfi.saved_mask & (1u << 30)) parse_err(where, "autiasp should be used after lr is saved", ctx);
    if (!(ctx->cfi.saved_mask & 1)) parse_err(where, "lr not signed", ctx);
    ctx->cfi.saved_mask -= 1;
    *out++ = CMD_CFI + 1;
    *out++ = DW_CFA_GNU_window_save;
    ctx->payloads_size = out - ctx->payloads;
    return;
  case KW_ret:
    if (ctx->cfi.sp_adj16 | ctx->cfi.fp_adj16 | ctx->cfi.saved_mask) parse_err(where, "unexpected state at ret.cfi", ctx);
    return;
  }
  parse_err(where, "unsupported .cfi suffix", ctx);
}

#define ror(x, n) (((x)<<(-(int)(n)&(8*sizeof(x)-1))) | ((x)>>(n)))

static void encode_insn(const uint8_t* bc, parsed_operands_t* operands, parse_ctx_t* ctx) {
  const uint8_t* bc0 = bc;
  bc += operands->n;
  uint32_t info = *bc++;
  uint32_t encoded = operands->operand_bits;
  if (info & ENCODING_SIZE_CONSTRAINT) bc++;
  if (info & 1) encoded ^= (uint32_t)*bc++;
  if (info & 2) encoded ^= (uint32_t)*bc++ << 8;
  if (info & 4) encoded ^= (uint32_t)*bc++ << 16;
  if (info & 8) encoded ^= (uint32_t)*bc++ << 24;
  if (info & 0xe0) FATAL("Unused info bits");

  uint8_t* out = ensure_payloads_space(ctx, 7 + sizeof(uint32_t));
  out[0] = 4;
  out[1] = (uint8_t)encoded;
  out[2] = (uint8_t)(encoded >> 8);
  out[3] = (uint8_t)(encoded >> 16);
  out[4] = (uint8_t)(encoded >> 24);
  out += 5;
  if (operands->need_reloc) {
    for (uint32_t i = 0; i < operands->n; ++i) {
      switch (bc0[i]) {
      case (operand_class_other << 6) + operand_class_other_rel16:
      case (operand_class_other << 6) + operand_class_other_rel21:
      case (operand_class_other << 6) + operand_class_other_rel28:
      case (operand_class_other << 6) + operand_class_other_rel21a:
      case (operand_class_other << 6) + operand_class_other_rel21l:
        *out++ = sizeof(uint32_t);
        memcpy(out, &operands->val[i].ident.token, sizeof(uint32_t));
        out += sizeof(uint32_t);
        *out++ = CMD_reloc + (bc0[i] & 0x3f);
        break;
      }
    }
  }
  ctx->payloads_size = out - ctx->payloads;
}

static uint32_t k13(uint64_t n, int is64) {
  /* Thanks to: https://dougallj.wordpress.com/2021/10/30/ */
  int rot, ones, size, immr, imms;
  if (!is64) n = ((uint64_t)n << 32) | (uint32_t)n;
  if ((n+1u) <= 1u) return 0;  /* Neither all-zero nor all-ones are allowed. */
  rot = (n & (n+1u)) ? __builtin_ctzll(n & (n+1u)) : 64;
  n = ror(n, rot & 63);
  ones = __builtin_ctzll(~n);
  size = __builtin_clzll(n) + ones;
  if (ror(n, size & 63) != n) return 0;  /* Non-repeating? */
  immr = -rot & (size - 1);
  imms = (-(size << 1) | (ones - 1)) & 63;
  return 0x18000000 ^ ((immr | (size & 64)) << 16) ^ (imms << 10);
}

static uint32_t k5(int64_t n) {
  uint64_t k = n < 0 ? ~(uint64_t)n+1u : (uint64_t)n;
  uint32_t m = n < 0 ? 0x40000000 : 0;
  if (k > 0x1f) {
    return 0;
  }
  return m ^ 0x00000800 ^ ((uint32_t)k << 16);
}

static uint32_t k12(int64_t n) {
  uint64_t k = n < 0 ? ~(uint64_t)n+1u : (uint64_t)n;
  uint32_t m = n < 0 ? 0x40000000 : 0;
  if (k < 0x1000) {
  } else if ((k & 0xfff000) == k) {
    k = (k >> 12) | (1u << 12);
  } else {
    return 0;
  }
  return m ^ 0x1a000000 ^ ((uint32_t)k << 10);
}

static bool set_fpr_Q(parsed_operands_t* operands, uint32_t q) {
  if (operands->inferred_size & INFERRED_Q_SET) {
    return (operands->operand_bits >> 30) == q;
  } else {
    operands->inferred_size |= INFERRED_Q_SET;
    operands->operand_bits ^= q << 30;
    return true;
  }
}

static bool set_fpr_size(parsed_operands_t* operands, uint32_t size) {
  if (operands->inferred_size & INFERRED_SIZE_SET) {
    return (operands->operand_bits >> 22) == size;
  } else {
    operands->inferred_size |= INFERRED_SIZE_SET;
    operands->operand_bits ^= size << 22;
    return true;
  }
}

static bool is_operand_ok(parsed_operands_t* operands, uint32_t idx, uint32_t bc, parse_ctx_t* ctx) {
  uint32_t clazz = bc >> 6;
  operand_kind_t actual = operands->kind[idx];
  switch (clazz) {
  case operand_class_gpr: {
    if (actual != operand_kind_gpr) return false;
    uint32_t variant = (bc >> 2) & 0xf;
    if (variant >= OPERAND_CLASS_GPR_R) { // R
      if (operands->val[idx].gpr.transform < gpr_transform_uxtb) {
        operands->inferred_size |= (operands->val[idx].gpr.index & (OPERAND_GPR_W | OPERAND_GPR_X));
      }
      variant -= OPERAND_CLASS_GPR_R;
    } else if (variant >= OPERAND_CLASS_GPR_X) { // X
      if (!(operands->val[idx].gpr.index & OPERAND_GPR_X)) return false;
      variant -= OPERAND_CLASS_GPR_X;
    } else { // W
      if (!(operands->val[idx].gpr.index & OPERAND_GPR_W)) return false;
    }
    if ((operands->val[idx].gpr.index & 31) == 31) {
      if ((variant == OPERAND_CLASS_GPR_SP) != !!(operands->val[idx].gpr.index & OPERAND_GPR_SP)) return false;
    }
    switch (variant) {
    case OPERAND_CLASS_GPR_SA:
      if (operands->val[idx].gpr.transform == gpr_transform_ror) return false;
      // fallthrough
    case OPERAND_CLASS_GPR_SL:
      if (operands->val[idx].gpr.transform > gpr_transform_ror) return false;
      if (operands->val[idx].gpr.transform == gpr_transform_none) break;
      operands->operand_bits ^= ((operands->val[idx].gpr.transform & 3u) << 22) ^ ((operands->val[idx].gpr.amount & 63u) << 10);
      break;
    case OPERAND_CLASS_GPR_XT:
      if (operands->val[idx].gpr.transform == gpr_transform_none) {
        operands->operand_bits ^= (operands->val[idx].gpr.index & OPERAND_GPR_X ? gpr_transform_uxtx : gpr_transform_uxtw) << 13;
        break;
      }
      if (operands->val[idx].gpr.transform < gpr_transform_uxtb) return false;
      operands->operand_bits ^= ((operands->val[idx].gpr.transform & 7u) << 13) ^ ((operands->val[idx].gpr.amount & 7u) << 10);
      break;
    default:
      if (operands->val[idx].gpr.transform != gpr_transform_none) return false;
      break;
    }
    uint32_t where = bc & 3;
    where = where * 5 + (where == 3);
    operands->operand_bits ^= (operands->val[idx].gpr.index & 31u) << where;
    return true; }
  case operand_class_fpr: {
    if (actual != operand_kind_fpr) return false;
    fpr_kind_t kind = (fpr_kind_t)operands->val[idx].fpr.kind;
    uint32_t lane_width = operands->val[idx].fpr.lane_width;
    switch ((operand_class_fpr_t)((bc >> 2) & 0xf)) {
    case operand_class_fpr_F:
      if (kind != fpr_kind_scalar) return false;
      if (lane_width < 1 || lane_width > 3) return false;
      if (!set_fpr_size(operands, lane_width ^ 2)) return false;
      break;
    case operand_class_fpr_FB:
      if (kind != fpr_kind_scalar) return false;
      if (lane_width > 3) return false;
      if (!set_fpr_size(operands, lane_width)) return false;
      break;
    case operand_class_fpr_VT:
      if (kind != fpr_kind_v8 && kind != fpr_kind_v16) return false;
      if (lane_width > 3) return false;
      if (!set_fpr_size(operands, lane_width)) return false;
      if (!set_fpr_Q(operands, kind == fpr_kind_v16)) return false;
      break;
    case operand_class_fpr_VTW:
      if (kind != fpr_kind_v8 && kind != fpr_kind_v16) return false;
      if (lane_width == 0 || lane_width > 3) return false;
      if (!set_fpr_size(operands, lane_width - 1)) return false;
      if (!set_fpr_Q(operands, kind == fpr_kind_v16)) return false;
      break;
    case operand_class_fpr_VD1:
      if (kind != fpr_kind_lane || lane_width != 3 || operands->val[idx].fpr.lane_number != 1) return false;
      break;
    case operand_class_fpr_VTidx_umov: {
      if (kind != fpr_kind_lane || lane_width > 3) return false;
      uint32_t bits = (operands->val[idx].fpr.lane_number * 2 + 1) << lane_width;
      if (!set_fpr_Q(operands, bits >> 5)) return false;
      operands->operand_bits ^= (bits & 0x1f) << 16;
      break; }
    default:
      FATAL("Bad operand pattern %02x", bc);
      break;
    }
    uint32_t where = bc & 3;
    where = where * 5 + (where == 3);
    operands->operand_bits ^= (operands->val[idx].fpr.index & 31u) << where;
    return true;
  }
  case operand_class_imm: {
    uint64_t value;
    if (actual == operand_kind_imm) value = operands->val[idx].imm;
    else if (actual == operand_kind_imm_lsl) value = (uint64_t)operands->val[idx].imm_lsl.imm << operands->val[idx].imm_lsl.lsl;
    else return false;
    switch ((operand_class_imm_t)(bc & 0x3f)) {
    case operand_class_imm_k5:
      value = k5(value);
      if (!value) return false;
      operands->operand_bits ^= value;
      return true;
    case operand_class_imm_k12:
      value = k12(value);
      if (!value) return false;
      operands->operand_bits ^= value;
      return true;
    case operand_class_imm_k13:
      value = k13(value, !(operands->inferred_size & OPERAND_GPR_W));
      if (!value) return false;
      if (value & (1u << 22)) operands->inferred_size |= OPERAND_GPR_X;
      operands->operand_bits ^= value;
      return true;
    case operand_class_imm_k16:
    case operand_class_imm_k16l: {
      uint32_t lsl;
      if (actual == operand_kind_imm_lsl) {
        lsl = operands->val[idx].imm_lsl.lsl;
        value = operands->val[idx].imm_lsl.imm;
        if (lsl & 15) return false;
      } else if (value) {
        lsl = __builtin_ctzll(value) & 0x30;
        value >>= lsl;
      } else {
        if ((operand_class_imm_t)(bc & 0x3f) == operand_class_imm_k16l) return false;
        lsl = 0;
      }
      if (value & ~(uint64_t)0xffff) {
        return false;
      }
      if (lsl && (operand_class_imm_t)(bc & 0x3f) == operand_class_imm_k16) return false;
      operands->operand_bits ^= (value << 5) ^ (lsl << 17);
      return true; }
    case operand_class_imm_kmov: {
      if (operands->inferred_size & OPERAND_GPR_W) {
        value = (uint32_t)value;
      } else if ((operands->inferred_size & OPERAND_GPR_X) && value == (uint32_t)value) {
        operands->inferred_size ^= OPERAND_GPR_X ^ OPERAND_GPR_W;
      }      
      uint32_t lsl = value ? __builtin_ctzll(value) & 0x30 : 0;
      value >>= lsl;
      if (value & ~(uint64_t)0xffff) {
        // Try inverted.
        value <<= lsl;
        value = ~value;
        if (operands->inferred_size & OPERAND_GPR_W) value = (uint32_t)value;
        lsl = value ? __builtin_ctzll(value) & 0x30 : 0;
        value >>= lsl;
        operands->operand_bits ^= 0x40000000;
        if (value & ~(uint64_t)0xffff) {
          return false;
        }
      }
      operands->operand_bits ^= (value << 5) ^ (lsl << 17);
      return true; }
    case operand_class_imm_ir:
      if (value > 63) return false;
      operands->operand_bits ^= value << 16;
      return true;
    case operand_class_imm_is:
      if (value > 63) return false;
      operands->operand_bits ^= value << 10;
      return true;
    case operand_class_imm_ilsl: {
      uint64_t limit = operands->inferred_size & OPERAND_GPR_X ? 63 : 31;
      if (value > limit) return false;
      operands->operand_bits ^= ((limit & 32) << 17) ^ ((-value & limit) << 16) ^ ((limit - value) << 10);
      return true; }
    case operand_class_imm_ib40:
      if (value > 63) {
        return false;
      } else if (value > 32) {
        operands->inferred_size |= OPERAND_GPR_X;
      } else {
        operands->inferred_size &= ~OPERAND_GPR_X;
        operands->inferred_size |= OPERAND_GPR_W;
      }
      operands->operand_bits ^= (value & 31) << 19;
      return true;
    case operand_class_imm_inzcv:
      if (value > 15) return false;
      operands->operand_bits ^= value;
      return true;
    case operand_class_imm_0:
      return value == 0;
    default:
      FATAL("Bad operand pattern %02x", bc);
      break;
    }
    break; }
  case operand_class_other: {
    switch ((operand_class_other_t)(bc & 0x3f)) {
    case operand_class_other_cc:
      if (operands->kind[idx] != operand_kind_ident) return false;
      if (IDENT_INFO_KIND(operands->val[idx].ident.info) != ident_kind_cc) return false;
      operands->operand_bits ^= IDENT_INFO_PAYLOAD(operands->val[idx].ident.info) << 12;
      return true;
    case operand_class_other_ccb:
      if (operands->kind[idx] != operand_kind_ident) return false;
      if (IDENT_INFO_KIND(operands->val[idx].ident.info) != ident_kind_cc) return false;
      operands->operand_bits ^= IDENT_INFO_PAYLOAD(operands->val[idx].ident.info);
      return true;
    case operand_class_other_rel16:
    case operand_class_other_rel21:
    case operand_class_other_rel28:
    case operand_class_other_rel21a:
      if (operands->kind[idx] != operand_kind_ident) return false;
      operands->need_reloc = true;
      return true;
    case operand_class_other_rel21l:
      if (operands->kind[idx] != operand_kind_addr_ident) return false;
      operands->need_reloc = true;
      return true;
    case operand_class_other_addr1:
    case operand_class_other_addr2:
    case operand_class_other_addr4:
    case operand_class_other_addr8: {
      if (operands->kind[idx] != operand_kind_addr) return false;
      uint8_t base_reg = operands->val[idx].addr.base_reg;
      uint32_t lsl = (operand_class_other_t)(bc & 0x3f) - operand_class_other_addr1;
      if (base_reg & ADDR_REG_INDEX) {
        if (base_reg & (ADDR_PRE | ADDR_POST)) return false;
        uint8_t amount = operands->val[idx].addr.index_reg.amount;
        if (amount != 0 && amount != lsl) return false;
        operands->operand_bits ^= ((base_reg & 31) << 5) ^ ((operands->val[idx].addr.index_reg.transform & 7) << 13) ^ ((operands->val[idx].addr.index_reg.index & 31) << 16) ^ (amount ? (1u << 12) : 0) ^ 0x1200800;
      } else {
        int32_t offset = operands->val[idx].addr.offset;
        if (base_reg & (ADDR_PRE | ADDR_POST)) {
          if (!S_OK_32(offset, 9)) return false;
          operands->operand_bits ^= (S_PACK(offset, 9) << 12) ^ ((base_reg & 31) << 5) ^ 0x1000400;
          if (base_reg & ADDR_PRE) operands->operand_bits ^= (1u << 11);
        } else if (ror((uint32_t)offset, lsl) & 0xfffff000) {
          if (!S_OK_32(offset, 9)) return false;
          operands->operand_bits ^= (S_PACK(offset, 9) << 12) ^ ((base_reg & 31) << 5) ^ 0x1000000;
        } else {
          operands->operand_bits ^= ((offset >> lsl) << 10) ^ ((base_reg & 31) << 5);
        }
      }
      return true;
    }
    case operand_class_other_addra: {
      if (operands->kind[idx] != operand_kind_addr) return false;
      if (operands->val[idx].addr.offset) return false;
      uint8_t base_reg = operands->val[idx].addr.base_reg;
      if (base_reg & ADDR_REG_INDEX) return false;
      operands->operand_bits ^= (base_reg & 31) << 5;
      return true; }
    case operand_class_other_addrp: {
      if (operands->kind[idx] != operand_kind_addr) return false;
      uint8_t base_reg = operands->val[idx].addr.base_reg;
      if (base_reg & ADDR_REG_INDEX) return false;
      int32_t offset = operands->val[idx].addr.offset;
      if (operands->inferred_size & OPERAND_GPR_X) {
        if (offset & 7) return false;
        offset >>= 3;
      } else {
        if (offset & 3) return false;
        offset >>= 2;
      }
      if (!S_OK_32(offset, 7)) return false;
      operands->operand_bits ^= (S_PACK(offset, 7) << 15) ^ ((base_reg & 31) << 5);
      if (base_reg & (ADDR_PRE | ADDR_POST)) {
        operands->operand_bits ^= 1u << 23;
        if (base_reg & ADDR_POST) {
          operands->operand_bits ^= 1u << 24;
        }
      }
      return true; }
    case operand_class_other_jc: {
      if (operands->kind[idx] != operand_kind_ident) return false;
      const char* key = sht_u_key(&ctx->toks.idents, operands->val[idx].ident.token);
      for (char c; (c = *key++) != '\0'; ) {
        if (c == 'c') operands->operand_bits ^= 1u << 6;
        else if (c == 'j') operands->operand_bits ^= 1u << 7;
        else return false;
      }
      return true; }
    default:
      FATAL("Bad operand pattern %02x", bc);
      break;
    }
    break; }
  default:
    FATAL("Bad operand pattern %02x", bc);
    break;
  }
  return false;
}

static const uint8_t* match_insn_overload(const uint8_t* bc, parsed_operands_t* operands, parse_ctx_t* ctx) {
  uint32_t n = operands->n;
  uint32_t i;
  for (;;) {
    operands->inferred_size = 0;
    operands->need_reloc = false;
    operands->operand_bits = 0;
    for (i = 0; i < n; ++i) {
      if (!is_operand_ok(operands, i, bc[i], ctx)) {
        goto next;
      }
    }
    if (operands->inferred_size & OPERAND_GPR_X) {
      if (operands->inferred_size & OPERAND_GPR_W) goto next;
      operands->operand_bits ^= 1u << 31;
    }
    if (operands->inferred_size & (INFERRED_Q_SET | INFERRED_SIZE_SET)) {
      uint8_t tail = bc[n];
      if (tail & ENCODING_SIZE_CONSTRAINT) {
        tail = *(bc + n + 1);
        tail >>= ((operands->operand_bits >> 22) & 3) ^ ((operands->operand_bits >> 28) & 4);
        if (!(tail & 1)) goto next;
      }
    }
    return bc;
  next:
    i = bc[-1];
    if (!i) return NULL;
    bc += i;
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

static token_t* parse_relative_offset_i32(token_t* src, parse_ctx_t* ctx) {
  if (src->type >= TOK_IDENT_THR) {
    parse_err(src, "expected identifier as operand to relative_offset_i32", ctx);
  }
  uint8_t* out = ensure_payloads_space(ctx, 1 + sizeof(uint32_t));
  *out++ = CMD_reloc + operand_class_other_rel32;
  memcpy(out, &src->type, sizeof(uint32_t));
  out += sizeof(uint32_t);
  ctx->payloads_size = out - ctx->payloads;
  return src + 1;
}

static token_t* parse_data(token_t* src, parse_ctx_t* ctx) {
  uint32_t tt = src->type;
  uint8_t imm_len;
  switch (tt) {
  case KW_byte:
    imm_len = 1;
    break;
  case KW_dword:
    imm_len = 4;
    break;
  case KW_qword:
    imm_len = 8;
    break;
  case KW_asciiz:
    return parse_asciiz(src + 1, ctx);
  case KW_relative_offset_i32:
    return parse_relative_offset_i32(src + 1, ctx);
  default:
    parse_err(src, "expected byte/dword/qword/asciiz keyword", ctx);
    return src;
  }
  ++src;
  for (;;) {
    uint64_t val;
    src = parse_imm(src, &val, ctx);
    uint8_t* out = ensure_payloads_space(ctx, 1 + imm_len);
    *out++ = imm_len;
    switch (imm_len) {
    case 1:
      *out++ = (uint8_t)val;
      break;
    case 4: {
      uint32_t u32 = (uint32_t)val;
      memcpy(out, &u32, 4);
      out += 4;
      break; }
    case 8:
      memcpy(out, &val, 8);
      out += 8;
      break;
    default:
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
  *out++ = CMD_reloc + operand_class_other_phantom_ref;
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
  uint32_t info = *(uint32_t*)sht_u_to_p(&ctx->toks.idents, tt);
  if (IDENT_INFO_KIND(info) != ident_kind_insn) {
    switch (tt) {
    case KW_align: return parse_align(src + 1, ctx);
    //case KW_u8_label_lut: return parse_u8_label_lut(src + 1, ctx); // TODO
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
  const uint8_t* bc = insn_encodings + IDENT_INFO_PAYLOAD(info);
  uint32_t narg = *bc++;
  parsed_operands_t operands;
  operands.n = 0;
  operands.operand_bits = 0;
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
    uint8_t delta = 1;
    if (narg_lo != narg_hi) {
      delta = bc[operands.n - narg_lo];
      if (!delta) {
        parse_err(insn_name, "wrong number of operands", ctx);
      }
    }
    bc = match_insn_overload(bc + delta, &operands, ctx);
    if (!bc) {
      parse_err(insn_name, "operands incompatible with instruction", ctx);
    }
  }
  encode_insn(bc, &operands, ctx);
  if (with_cfi) {
    encode_insn_cfi(&operands, ctx, with_cfi);
  }
  return src;
}

static void enter_gen_keywords(sht_t* idents) {
  const char* key = GEN_OPERAND_STRINGS;
  const uint32_t* value = gen_operand_infos;
  for (uint32_t len; (len = (unsigned char)*key++); key += len) {
    *(uint32_t*)sht_intern_p(idents, key, len) = *value++;
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
      if (flags & ASM_SYM_FLAG_EXTERN) {
        parse_err(t, "extern cannot be applied to variables yet", ctx);
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
        ctx->cfi.sp_adj16 = 0;
        ctx->cfi.fp_adj16 = 0;
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

static uint64_t pack_runtime_reloc(uint8_t kind, uint32_t where, uint32_t target) {
  return kind + (where << 8) + ((uint64_t)target << 32);
}

static void resolve_labels_for_data(parse_ctx_t* ctx, asm_sym_t* sym) {
  const uint8_t* payload_start = ctx->payloads + sym->payload_start;
  const uint8_t* payload_end = ctx->payloads + sym->payload_end;
  uint32_t size = 0;
  uint32_t align = 1;
  uint32_t nreloc = 0;
  for (const uint8_t* itr = payload_start; itr < payload_end; ) {
    uint8_t head = *itr++;
    if (head < CMD_reloc) {
      itr += head;
    } else {
      itr += 4;
      nreloc += 1;
      head = 4;
    }
    size += head;
    if (head > align && !(head & (head - 1))) {
      align = head;
    }
  }
  ensure_payloads_space(ctx, size + 16 + nreloc * sizeof(uint64_t));
  ctx->payloads_size = (ctx->payloads_size + 7u) & ~7u;
  uint16_t* head_out = (uint16_t*)(ctx->payloads + ctx->payloads_size);
  head_out[0] = 0; // Will become name length and flags.
  head_out[1] = nreloc;
  head_out[2] = size;
  head_out[3] = align << 8; // Will gain allocation category in low byte.
  uint64_t* reloc_out = (uint64_t*)(head_out + 4);
  uint8_t* out = (uint8_t*)(reloc_out + nreloc);
  uint8_t* out0 = out;
  payload_start = ctx->payloads + sym->payload_start;
  payload_end = ctx->payloads + sym->payload_end;
  nreloc = 0;
  for (const uint8_t* itr = payload_start; itr < payload_end; ) {
    uint8_t head = *itr++;
    if (head < CMD_reloc) {
      memcpy(out, itr, head);
      itr += head;
      out += head;
    } else {
      uint32_t target;
      memcpy(&target, itr, 4);
      itr += 4;
      uint64_t k = uuht_contains(&ctx->sym_names, target);
      if (!k) {
        FATAL("%s contains reference to undefined %s", sht_u_key(&ctx->toks.idents, sym->name), sht_u_key(&ctx->toks.idents, target));
      }
      switch (head) {
      case CMD_reloc + operand_class_other_rel32:
        reloc_out[nreloc++] = pack_runtime_reloc(head - CMD_reloc, out - out0, k >> 32);
        out += 4;
        break;
      default:
        FATAL("Bad data reloc kind %u", head);
      }
    }
  }
  enum erw_alloc_category cat;
  if (sym->flags & ASM_SYM_FLAG_CONST) {
    cat = erw_alloc_category_v_r_near_x;
  } else if (!nreloc && is_zero_mem(out - size, size)) {
    cat = erw_alloc_category_v_rw_zero;
    out -= size;
  } else {
    cat = erw_alloc_category_v_rw;
  }
  head_out[3] |= cat;
  sym->payload_start = ctx->payloads_size;
  sym->payload_end = ctx->payloads_size = out - ctx->payloads;
}

typedef union resolve_reloc_t {
  uint64_t runtime;
  struct {
    uint8_t head;
    uint32_t value;
  };
} resolve_reloc_t;

typedef struct resolve_state_t {
  uuht_t local_labels; // sht index to reloc index
  resolve_reloc_t* relocs;
  uint32_t* positions;
  uint8_t* code;
  uint8_t* cfi;  
  uint32_t relocs_capacity;
  uint32_t code_capacity;
  uint32_t cfi_capacity;
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
  uint32_t code_size_bound = 0;
  for (const uint8_t* itr = payload_start; itr < payload_end; ) {
    uint8_t head = *itr++;
    if (head < CMD_CFI) {
      code_size_bound += head;
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
  if (reloc_idx >= rs->relocs_capacity) {
    rs->relocs_capacity = reloc_idx + 1;
    free(rs->relocs);
    rs->relocs = malloc(rs->relocs_capacity * (sizeof(rs->relocs[0]) + sizeof(rs->positions[0])));
    rs->positions = (uint32_t*)(rs->relocs + rs->relocs_capacity);
  }
  if (code_size_bound > rs->code_capacity) {
    rs->code_capacity = code_size_bound + (code_size_bound >> 1);
    free(rs->code);
    rs->code = malloc(rs->code_capacity);
  }
  if (cfi_size_bound > rs->cfi_capacity) {
    rs->cfi_capacity = cfi_size_bound + (cfi_size_bound >> 1);
    free(rs->cfi);
    rs->cfi = malloc(rs->cfi_capacity);
  }

  // Split out code/cfi/relocs.
  rs->positions[0] = 0;
  reloc_idx = 0;
  uint8_t* cfi_out = rs->cfi;
  uint32_t code_size = 0;
  uint32_t last_cfi_off = 0;
  for (const uint8_t* itr = payload_start; itr < payload_end; ) {
    uint8_t head = *itr++;
    if (head < CMD_CFI) {
      memcpy(rs->code + code_size, itr, head);
      itr += head;
      code_size += head;
    } else if (head < CMD_reloc) {
      if (last_cfi_off != code_size) {
        cfi_out = emit_cfi_advance(cfi_out, code_size - last_cfi_off);
        last_cfi_off = code_size;
      }
      head -= CMD_CFI;
      memcpy(cfi_out, itr, head);
      itr += head;
      cfi_out += head;
    } else {
      ++reloc_idx;
      rs->relocs[reloc_idx].head = head;
      switch (head) {
      case CMD_reloc + operand_class_other_rel16:
      case CMD_reloc + operand_class_other_rel21:
      case CMD_reloc + operand_class_other_rel28:
      case CMD_reloc + operand_class_other_rel21a:
      case CMD_reloc + operand_class_other_rel21l:
      case CMD_reloc + operand_class_other_phantom_ref:
        code_size -= sizeof(uint32_t);
        memcpy(&rs->relocs[reloc_idx].value, rs->code + code_size, sizeof(uint32_t));
        break;
      case CMD_reloc + RELOC_def_label:
        code_size -= sizeof(uint32_t);
        break;
      case CMD_reloc + RELOC_align: {
        uint32_t amt = rs->code[--code_size];
        amt = (amt + 1 - code_size) & amt;
        if (amt & 3) FATAL("align gap size not a multiple of four bytes");
        // TODO: if amt > 16, put down a branch
        for (; amt; code_size += 4, amt -= 4) {
          memcpy(rs->code + code_size, "\x1f\x20\x03\xd5", 4); // nop
        }
        break; }
      default:
        FATAL("bad reloc cmd 0x%02x", head);
      }
      ASSERT(!(code_size & 3));
      rs->positions[reloc_idx] = code_size;
    }
  }
  ASSERT(!(code_size & 3));
  uint32_t cfi_size = cfi_out - rs->cfi;

  // Apply relocs that need positions.
  uint32_t n_runtime_reloc = 0;
  for (uint32_t i = 1; i <= reloc_idx; ++i) {
    uint8_t head = rs->relocs[i].head;
    switch (head) {
    case CMD_reloc + operand_class_other_rel16:
    case CMD_reloc + operand_class_other_rel21:
    case CMD_reloc + operand_class_other_rel28: {
      uint32_t target = rs->relocs[i].value;
      uint64_t k = uuht_contains(&rs->local_labels, target);
      uint32_t our_pos = rs->positions[i] - 4;
      if (k) {
        int32_t delta = (int32_t)(uint32_t)(rs->positions[k >> 32] - our_pos);
        if (!encode_reloc((operand_class_other_t)(head - CMD_reloc), rs->code + our_pos, delta)) {
          FATAL("%s contains unencodable branch to %s", sht_u_key(&ctx->toks.idents, sym->name), sht_u_key(&ctx->toks.idents, target));
        }
      } else if ((k = uuht_contains(&ctx->sym_names, target))) {
        if (!(ctx->syms[k >> 32].flags & ASM_SYM_FLAG_FUNCTION)) {
          FATAL("%s contains branch to %s, which is not a function", sht_u_key(&ctx->toks.idents, sym->name), sht_u_key(&ctx->toks.idents, target));
        }
        rs->relocs[n_runtime_reloc++].runtime = pack_runtime_reloc(head - CMD_reloc, our_pos, k >> 32);
      } else {
        FATAL("%s contains branch to undefined %s", sht_u_key(&ctx->toks.idents, sym->name), sht_u_key(&ctx->toks.idents, target));
      }
      break; }
    case CMD_reloc + operand_class_other_rel21a: {
      uint32_t target = rs->relocs[i].value;
      uint64_t k = uuht_contains(&rs->local_labels, target);
      uint32_t our_pos = rs->positions[i] - 4;
      if (k) {
        int32_t delta = (int32_t)(uint32_t)(rs->positions[k >> 32] - our_pos);
        if (!encode_reloc((operand_class_other_t)(head - CMD_reloc), rs->code + our_pos, delta)) {
          FATAL("%s contains unencodable adr of %s", sht_u_key(&ctx->toks.idents, sym->name), sht_u_key(&ctx->toks.idents, target));
        }
      } else if ((k = uuht_contains(&ctx->sym_names, target))) {
        rs->relocs[n_runtime_reloc++].runtime = pack_runtime_reloc(head - CMD_reloc, our_pos, k >> 32);
      } else {
        FATAL("%s contains adr of undefined %s", sht_u_key(&ctx->toks.idents, sym->name), sht_u_key(&ctx->toks.idents, target));
      }
      break; }
    case CMD_reloc + operand_class_other_rel21l: {
      uint32_t target = rs->relocs[i].value;
      uint64_t k = uuht_contains(&rs->local_labels, target);
      uint32_t our_pos = rs->positions[i] - 4;
      if (k) {
        FATAL("%s contains ldr of label %s", sht_u_key(&ctx->toks.idents, sym->name), sht_u_key(&ctx->toks.idents, target));
      } else if ((k = uuht_contains(&ctx->sym_names, target))) {
        if (ctx->syms[k >> 32].flags & ASM_SYM_FLAG_FUNCTION) {
          FATAL("%s contains ldr of function %s", sht_u_key(&ctx->toks.idents, sym->name), sht_u_key(&ctx->toks.idents, target));
        }
        rs->relocs[n_runtime_reloc++].runtime = pack_runtime_reloc(head - CMD_reloc, our_pos, k >> 32);
      } else {
        FATAL("%s contains ldr of undefined %s", sht_u_key(&ctx->toks.idents, sym->name), sht_u_key(&ctx->toks.idents, target));
      }
      break; }
    case CMD_reloc + operand_class_other_phantom_ref: {
      uint32_t target = rs->relocs[i].value;
      uint64_t k = uuht_contains(&ctx->sym_names, target);
      if (k) {
        rs->relocs[n_runtime_reloc++].runtime = pack_runtime_reloc(head - CMD_reloc, 0, k >> 32);
      } else {
        FATAL("%s contains phantom_ref of undefined %s", sht_u_key(&ctx->toks.idents, sym->name), sht_u_key(&ctx->toks.idents, target));
      }
      break; }
    case CMD_reloc + RELOC_def_label:
    case CMD_reloc + RELOC_align:
      // No work required this pass.
      break;
    default:
      FATAL("bad reloc cmd");
    }
  }

  ensure_payloads_space(ctx, 16 + n_runtime_reloc * sizeof(uint64_t) + code_size + cfi_size);
  ctx->payloads_size = (ctx->payloads_size + 7u) & ~7u;
  uint16_t* head_out = (uint16_t*)(ctx->payloads + ctx->payloads_size);
  head_out[0] = 0; // Will become name length and flags.
  head_out[1] = n_runtime_reloc;
  head_out[2] = code_size;
  head_out[3] = cfi_size;
  uint8_t* out = (uint8_t*)(head_out + 4);
  memcpy(out, rs->relocs, n_runtime_reloc * sizeof(uint64_t));
  out += n_runtime_reloc * sizeof(uint64_t);
  memcpy(out, rs->code, code_size);
  out += code_size;
  memcpy(out, rs->cfi, cfi_size);
  out += cfi_size;
  sym->payload_start = ctx->payloads_size;
  sym->payload_end = ctx->payloads_size = out - ctx->payloads;
}

static void resolve_labels(parse_ctx_t* ctx) {
  resolve_state_t rs;
  memset(&rs, 0, sizeof(rs));
  uuht_init(&rs.local_labels);
  for (asm_sym_t *sym = ctx->syms, *end = sym + ctx->syms_size; sym < end; ++sym) {
    uint32_t flags = sym->flags;
    if (flags & ASM_SYM_FLAG_EXTERN) continue;
    if (flags & ASM_SYM_FLAG_FUNCTION) {
      resolve_labels_for_func(ctx, sym, &rs);
    } else {
      resolve_labels_for_data(ctx, sym);
    }
  }
  free(rs.relocs);
  free(rs.code);
  free(rs.cfi);
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
      off = (off + 7u) &~ 7u;
      uint32_t hash = sht_hash_mem(name_str, name_len);
      rh_insert(hash_table, hash_mask, hash, off);
    }
    ctx->payloads[sym->payload_start] = flags; // Save flags in payload
    off = (off + 7u) &~ 7u;
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
      uint64_t* relocs = (uint64_t*)(head + 4);
      for (uint32_t i = 0; i < n_reloc; ++i) {
        uint64_t val = relocs[i];
        uint64_t target = ctx->syms[val >> 32].flags; // Not flags at this point
        relocs[i] = (uint32_t)val | (target << 32);
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
    if (off & 7) {
      uint32_t aligned = (off + 7u) &~ 7u;
      emit_bytes((uint8_t*)"\0\0\0\0\0\0", aligned - off, f);
      off = aligned;
    }
    const char* key = sht_u_key(&ctx->toks.idents, sym->name);
    if (len) {
      emit_bytes((uint8_t*)key, len, f);
    }
    fprintf(f, "/* %s */", key);
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
  enter_gen_keywords(&ctx.toks.idents);

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
