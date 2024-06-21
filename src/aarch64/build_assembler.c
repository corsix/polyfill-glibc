#include <string.h>
#include "../tokenise.h"
#include "../common.h"
#include "../uuht.h"
#include "assembler.h"

#define KWS(KW) \
  KW("\005", sizes) \
  KW("\002", Rd) \
  KW("\002", Rn) \
  KW("\002", Rm) \
  KW("\002", Ra) \
  KW("\002", Wd) \
  KW("\002", Wn) \
  KW("\002", Wm) \
  KW("\002", Wa) \
  KW("\002", Xd) \
  KW("\002", Xn) \
  KW("\002", Xm) \
  KW("\002", Xa) \
  KW("\002", sp) \
  KW("\002", sa) \
  KW("\002", sl) \
  KW("\002", xt) \
  KW("\002", Fd) \
  KW("\002", Fn) \
  KW("\002", Fm) \
  KW("\002", Fa) \
  KW("\002", Vd) \
  KW("\002", Vn) \
  KW("\002", Vm) \
  KW("\002", Va) \
  KW("\001", B) \
  KW("\001", T) \
  KW("\002", TW) \
  KW("\002", D1) \
  KW("\011", Tidx_umov) \
  KW("\002", jc) \
  KW("\002", cc) \
  KW("\003", ccb) \
  KW("\002", k5) \
  KW("\003", k12) \
  KW("\003", k13) \
  KW("\003", k16) \
  KW("\004", k16l) \
  KW("\004", kmov) \
  KW("\002", ir) \
  KW("\002", is) \
  KW("\004", ilsl) \
  KW("\004", ib40) \
  KW("\005", inzcv) \
  KW("\005", rel16) \
  KW("\005", rel21) \
  KW("\005", rel28) \
  KW("\006", rel21a) \
  KW("\007", rel21ap) \
  KW("\006", rel21l) \
  KW("\005", addr1) \
  KW("\005", addr2) \
  KW("\005", addr4) \
  KW("\005", addr8) \
  KW("\005", addra) \
  KW("\005", addrp)
#include "../sht_keywords.h"

typedef struct overload_desc_t {
  uint8_t n_operands;
  uint8_t allowed_sizes;
  uint32_t encoding;
  uint32_t operand_base[4];
  uint32_t operand_modifier[4];
} overload_desc_t;

typedef struct insn_desc_t {
  overload_desc_t* overloads;
  uint32_t overloads_size;
  uint32_t overloads_capacity;
} insn_desc_t;

typedef struct isa_desc_t {
  uuht_t names; // key is toks.sht index, value is index into insns
  insn_desc_t* insns;
  uint32_t insns_size;
  uint32_t insns_capacity;
} isa_desc_t;

static overload_desc_t* alloc_new_overload(isa_desc_t* isa, uint32_t insn_name) {
  uint32_t insn_index = uuht_lookup_or_set(&isa->names, insn_name, isa->insns_size);
  if (insn_index >= isa->insns_size) {
    if (insn_index >= isa->insns_capacity) {
      uint32_t new_capacity = insn_index ? insn_index * 2 : 128;
      isa->insns = realloc(isa->insns, new_capacity * sizeof(isa->insns[0]));
      memset(isa->insns + isa->insns_capacity, 0, (new_capacity - isa->insns_capacity) * sizeof(isa->insns[0]));
      isa->insns_capacity = new_capacity;
    }
    isa->insns_size = insn_index + 1;
  }
  insn_desc_t* insn = isa->insns + insn_index;
  uint32_t overload_index = insn->overloads_size;
  if (overload_index >= insn->overloads_capacity) {
    uint32_t new_capacity = overload_index ? overload_index * 2 : 2;
    insn->overloads = realloc(insn->overloads, new_capacity * sizeof(insn->overloads[0]));
    memset(insn->overloads + insn->overloads_capacity, 0, (new_capacity - insn->overloads_capacity) * sizeof(insn->overloads[0]));
    insn->overloads_capacity = new_capacity;
  }
  insn->overloads_size = overload_index + 1;
  return insn->overloads + overload_index;
}

static int u64_asc(const void* lhsp, const void* rhsp) {
  uint64_t lhs = *(const uint64_t*)lhsp;
  uint64_t rhs = *(const uint64_t*)rhsp;
  if (lhs == rhs) return 0;
  return lhs < rhs ? -1 : 1;
}

static const char* encode_operand(sht_t* idents, uint32_t base, uint32_t modifier) {
#define GPR3(c0, c1x, m, mx) case m: return "(operand_class_gpr << 6) ^ ((OPERAND_CLASS_GPR_" #c0 " + OPERAND_CLASS_GPR_" #mx ") << 2) ^ " #c1x;
#define GPR2(c0, c1, c1x) case KW_##c0##c1: switch (modifier) { GPR3(c0, c1x, 0, PLAIN) GPR3(c0, c1x, KW_sp, SP) GPR3(c0, c1x, KW_sa, SA) GPR3(c0, c1x, KW_sl, SL) GPR3(c0, c1x, KW_xt, XT) } break;
#define GPR1(c0) GPR2(c0, d, 0) GPR2(c0, n, 1) GPR2(c0, a, 2) GPR2(c0, m, 3);
#define IMM(c) case KW_##c: if (modifier) break; return "(operand_class_imm << 6) ^ operand_class_imm_" #c;
#define OTHER(c) case KW_##c: if (modifier) break; return "(operand_class_other << 6) ^ operand_class_other_" #c;
  switch (base) {
  GPR1(R) GPR1(W) GPR1(X)
  IMM(k5) IMM(k12) IMM(k13) IMM(k16) IMM(k16l) IMM(kmov)
  IMM(ir) IMM(is) IMM(ilsl) IMM(ib40) IMM(inzcv)
  OTHER(cc) OTHER(ccb) OTHER(jc)
  OTHER(rel16) OTHER(rel21) OTHER(rel28) OTHER(rel21a) OTHER(rel21ap) OTHER(rel21l)
  OTHER(addr1) OTHER(addr2) OTHER(addr4) OTHER(addr8) OTHER(addra) OTHER(addrp)
#define FPR2(c0, c1, c1x, x) case KW_##c0##c1: switch (modifier) { x(c1x) } break;
#define FPR1(c0, x) FPR2(c0, d, 0, x) FPR2(c0, n, 1, x) FPR2(c0, a, 2, x) FPR2(c0, m, 3, x)
#define Fmods(c1x) \
  case 0: return "(operand_class_fpr << 6) ^ (operand_class_fpr_F << 2) ^ " #c1x; \
  case KW_B: return "(operand_class_fpr << 6) ^ (operand_class_fpr_FB << 2) ^ " #c1x;
  FPR1(F, Fmods)
#undef Fmods
#define Vmods(c1x) \
  case KW_T: return "(operand_class_fpr << 6) ^ (operand_class_fpr_VT << 2) ^ " #c1x; \
  case KW_TW: return "(operand_class_fpr << 6) ^ (operand_class_fpr_VTW << 2) ^ " #c1x; \
  case KW_D1: return "(operand_class_fpr << 6) ^ (operand_class_fpr_VD1 << 2) ^ " #c1x; \
  case KW_Tidx_umov: return "(operand_class_fpr << 6) ^ (operand_class_fpr_VTidx_umov << 2) ^ " #c1x;
  FPR1(V, Vmods)
#undef Vmods
  }
#undef FPR1
#undef FPR2
#undef OTHER
#undef IMM
#undef GPR1
#undef GPR2
#undef GPR3
  FATAL("Cannot encode operand %s%s%s", sht_u_key(idents, base), modifier ? "|" : "", modifier ? sht_u_key(idents, modifier) : "");
  return NULL;
}

#define HAS_INSN 0x01
#define HAS_TRANSFORM 0x02
#define HAS_REG 0x04
#define HAS_CC 0x08
#define HAS_LANE_WIDTH 0x10

static void build_insn_encodings(isa_desc_t* isa, sht_t* idents, FILE* out) {
  fputs("static const uint8_t insn_encodings[] = {\n", out);
  uint32_t insn_encodings_size = 0;
  qsort(isa->names.table, isa->names.mask + 1, sizeof(uint64_t), u64_asc);
  uint8_t bytes_u[256];
  const char* bytes[256];
  uint32_t n_bytes = 0;
  uint32_t overload_tail[16];
  for (uint32_t i = 0, ilim = isa->names.mask; i <= ilim; ++i) {
    uint64_t slot = isa->names.table[i];
    if (!slot) continue;
    void* name_p = sht_u_to_p(idents, uuht_unhash((uint32_t)slot));
    *(uint32_t*)name_p = (insn_encodings_size << 8) | HAS_INSN;
    insn_desc_t* insn = isa->insns + (slot >> 32);
    uint8_t narg_lo = 255;
    uint8_t narg_hi = 0;
    for (uint32_t j = 0, jlim = insn->overloads_size; j < jlim; ++j) {
      uint8_t narg = insn->overloads[j].n_operands;
      if (narg > narg_hi) narg_hi = narg;
      if (narg < narg_lo) narg_lo = narg;
      overload_tail[narg] = 0;
    }
    fprintf(out, "/* %s */%c0x%02x", sht_p_key(name_p), insn_encodings_size ? ',' : ' ', (narg_hi << 4) ^ narg_lo);
    insn_encodings_size += 1;
    n_bytes = 0;
    if (narg_lo != narg_hi) {
      for (uint32_t j = narg_lo; j <= narg_hi; ++j) {
        bytes[n_bytes++] = "0"; // offset to start of chain
      }
    }
    for (uint32_t j = 0, jlim = insn->overloads_size; j < jlim; ++j) {
      overload_desc_t* overload = insn->overloads + j;
      if (narg_hi) {
        bytes[n_bytes++] = "0"; // offset of next overload
      }
      uint8_t narg = overload->n_operands;
      uint32_t narg_tail = overload_tail[narg];
      if (narg_tail == 0) {
        if (narg_lo != narg_hi) {
          bytes[narg - narg_lo] = NULL;
          bytes_u[narg - narg_lo] = n_bytes;
        }
      } else {
        bytes[narg_tail - 1] = NULL;
        bytes_u[narg_tail - 1] = n_bytes - narg_tail;
      }
      overload_tail[narg] = n_bytes;
      for (uint32_t k = 0, klim = overload->n_operands; k < klim; ++k) {
        bytes[n_bytes++] = encode_operand(idents, overload->operand_base[k], overload->operand_modifier[k]);
      }
      uint8_t control = 0;
      for (uint32_t k = 0; k < 4; ++k) {
        if ((overload->encoding >> (8 * k)) & 0xff) {
          control |= 1 << k;
        }
      }
      if (overload->allowed_sizes != 0 && overload->allowed_sizes < 0xff) {
        control |= ENCODING_SIZE_CONSTRAINT;
      }
      bytes[n_bytes] = NULL;
      bytes_u[n_bytes++] = control;
      if (control & ENCODING_SIZE_CONSTRAINT) {
        bytes[n_bytes] = NULL;
        bytes_u[n_bytes++] = (uint8_t)overload->allowed_sizes;
      }
      for (uint32_t k = 0; k < 4; ++k) {
        uint8_t b = (overload->encoding >> (8 * k)) & 0xff;
        if (b) {
          bytes[n_bytes] = NULL;
          bytes_u[n_bytes++] = b;
        }
      }
    }
    free(insn->overloads);
    for (uint32_t j = 0; j < n_bytes; ++j) {
      if (bytes[j]) {
        fprintf(out, ", %s", bytes[j]);
      } else {
        fprintf(out, ", 0x%02x", bytes_u[j]);
      }
    }
    fputs("\n", out);
    insn_encodings_size += n_bytes;
  }
  fputs("};\n", out);
  free(isa->insns);
  uuht_free(&isa->names);
}

int main(int argc, const char** argv) {
  if (argc < 3) {
    FATAL("Usage: %s in_filename out_filename", argv[0]);
  }
  FILE* out = fopen(argv[2], "w");
  if (!out) {
    FATAL("Could not open %s for writing", argv[2]);
  }

  tokeniser_t toks;
  tokeniser_init(&toks);
  enter_keywords(&toks.idents);
  tokeniser_load_file(&toks, argv[1]);
  tokeniser_run(&toks);

  isa_desc_t isa;
  memset(&isa, 0, sizeof(isa));
  uuht_init(&isa.names);

  token_t* t = toks.tokens;
  uint32_t tt = t->type;
  for (;;) {
    if (tt >= TOK_IDENT_THR) {
      if (tt == TOK_EOF) break;
      tokeniser_error(&toks, t, "expected identifier");
    }
    overload_desc_t* overload = alloc_new_overload(&isa, tt);
    tt = (++t)->type;
    while (tt != (TOK_PUNCT(':') + PUNCT_REPEAT)) {
      if (overload->n_operands == sizeof(overload->operand_base)/sizeof(overload->operand_base[0])) tokeniser_error(&toks, t, "too many operands");
      if (tt > TOK_IDENT_THR) tokeniser_error(&toks, t, "expected identifier");
      overload->operand_base[overload->n_operands] = tt;
      overload->operand_modifier[overload->n_operands] = 0;
      tt = (++t)->type;
      if (tt == TOK_PUNCT('|')) {
        tt = (++t)->type;
        if (tt > TOK_IDENT_THR) tokeniser_error(&toks, t, "expected identifier");
        overload->operand_modifier[overload->n_operands] = tt;
        tt = (++t)->type;
      }
      overload->n_operands += 1;
    }
    tt = (++t)->type;
    if (tt != TOK_NUMBER) tokeniser_error(&toks, t, "expected number");
    uint64_t encoding;
    if (!tokeniser_decode_uint(&toks, t, &encoding) || encoding != (uint32_t)encoding) tokeniser_error(&toks, t, "invalid number");
    overload->encoding = (uint32_t)encoding;
    tt = (++t)->type;
    if (tt == KW_sizes && t[1].type == TOK_NUMBER && tokeniser_decode_uint(&toks, t+1, &encoding) && 0 < encoding && encoding < 0xff) {
      overload->allowed_sizes = (uint8_t)encoding;
      t += 2;
      tt = t->type;
    }
  }

  build_insn_encodings(&isa, &toks.idents, out);

#define TRANSFORM(x) *(uint32_t*)sht_intern_p(&toks.idents, #x, sizeof(#x)-1) |= HAS_TRANSFORM
  TRANSFORM(lsl);
  TRANSFORM(lsr);
  TRANSFORM(asr);
  TRANSFORM(ror);
  TRANSFORM(uxtb);
  TRANSFORM(uxth);
  TRANSFORM(uxtw);
  TRANSFORM(uxtx);
  TRANSFORM(sxtb);
  TRANSFORM(sxth);
  TRANSFORM(sxtw);
  TRANSFORM(sxtx);
#undef TRANSFORM
#define CC(x, n) *(uint32_t*)sht_intern_p(&toks.idents, #x, sizeof(#x)-1) |= HAS_CC | (n << 8);
  CC(eq, 0);
  CC(ne, 1);
  CC(cs, 2); CC(hs, 2);
  CC(cc, 3); CC(lo, 3);
  CC(mi, 4);
  CC(pl, 5);
  CC(vs, 6);
  CC(vc, 7);
  CC(hi, 8);
  CC(ls, 9);
  CC(ge, 10);
  CC(lt, 11);
  CC(gt, 12);
  CC(le, 13);
  CC(al, 14);
#undef CC
#define LW(c, n) *(uint32_t*)sht_intern_p(&toks.idents, #c, sizeof(#c)-1) |= HAS_LANE_WIDTH | (n << 8);
  LW(B, 0);
  LW(H, 1);
  LW(S, 2);
  LW(D, 3);
  LW(Q, 4);
#undef LW
  for (uint32_t i = 0; i < 31; ++i) {
    if (i == 18) continue; // x18 reserved for platform use
    char buf[8];
    uint32_t n = (uint32_t)sprintf(buf, " %u", i);
    for (const char* chars = "wx"; *chars; ++chars) {
      buf[0] = *chars;
      *(uint32_t*)sht_intern_p(&toks.idents, buf, n) |= HAS_REG | (i << 8);
    }
  }
  *(uint32_t*)sht_intern_p(&toks.idents, "fp", 2) |= HAS_REG | (29 << 8);
  *(uint32_t*)sht_intern_p(&toks.idents, "lr", 2) |= HAS_REG | (30 << 8);
  *(uint32_t*)sht_intern_p(&toks.idents, "sp", 2) |= HAS_REG | (31 << 8);
  *(uint32_t*)sht_intern_p(&toks.idents, "wzr", 3) |= HAS_REG | (31 << 8);
  *(uint32_t*)sht_intern_p(&toks.idents, "xzr", 3) |= HAS_REG | (31 << 8);
  for (uint32_t i = 0; i < 32; ++i) {
    char buf[8];
    uint32_t n = (uint32_t)sprintf(buf, " %u", i);
    uint32_t sz = 0;
    for (const char* chars = "bhsdqv"; *chars; ++chars, ++sz) {
      buf[0] = *chars;
      if (sz > 4) sz = 8|4;
      *(uint32_t*)sht_intern_p(&toks.idents, buf, n) |= HAS_REG | (i << 8) | (sz << 13) | (1u << 17);
    }
  }

  fputs("static const uint32_t gen_operand_infos[] = {\n", out);
  for (void* itr = sht_iter_start_p(&toks.idents); itr; itr = sht_iter_next_p(&toks.idents, itr)) {
    uint32_t info = *(uint32_t*)itr;
    if (!info) continue;
    const char* key = sht_p_key(itr);
    fprintf(out, "/* %s */", key);
    const char* kind;
    if (info & HAS_INSN) {
      fprintf(out, "%u + ", info & ~0xffu);
      kind = "ident_kind_insn";
    } else if (info & HAS_REG) {
      fputs("((", out);
      if (info <= 0x1fff) {
        if (key[0] == 'w') fputs("OPERAND_GPR_W", out);
        else if (key[0] == 'x') fputs("OPERAND_GPR_X", out);
        else if (!strcmp(key, "sp")) fputs("OPERAND_GPR_X + OPERAND_GPR_SP", out);
        else if (!strcmp(key, "fp") || !strcmp(key, "lr")) fputs("OPERAND_GPR_X", out);
        else FATAL("Unknown GPR %s", key);
        kind = "ident_kind_gpr";
      } else {
        kind = "ident_kind_fpr";
        info &= 0x1ffff;
      }
      fprintf(out, " + %u) << 8) + ", info >> 8);
    } else if (info & HAS_CC) {
      fprintf(out, "(%u << 8) + ", info >> 8);
      kind = "ident_kind_cc";
    } else if (info & HAS_LANE_WIDTH) {
      fprintf(out, "(%u << 8) + ", info >> 8);
      kind = "ident_kind_lane_width";
    } else {
      kind = "ident_kind_none";
    }
    if (info & HAS_TRANSFORM) {
      fprintf(out, "(gpr_transform_%s << 4) + ", key);
    }
    fprintf(out, "%s,\n", kind);
  }
  fputs("0};\n", out);
  fputs("#define GEN_OPERAND_STRINGS \\\n", out);
  for (void* itr = sht_iter_start_p(&toks.idents); itr; itr = sht_iter_next_p(&toks.idents, itr)) {
    uint32_t info = *(uint32_t*)itr;
    if (!info) continue;
    const char* key = sht_p_key(itr);
    fprintf(out, "\"\\%03o\" \"%s\" \\\n", sht_p_key_length(itr), key);
  }
  fputs("\"\"\n", out);

  tokeniser_free(&toks);
  fclose(out);
  return 0;
}
