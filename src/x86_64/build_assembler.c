#include <string.h>
#include "../tokenise.h"
#include "../common.h"
#include "assembler.h"

#define KWS(KW) \
  /* Operands must be first */\
  KW("\001", r,      (OP_CLASS_R << 6) + (R_ENCODE_STYLE_MODRM_REG << 4) + SIZE_CLASS_GPR) \
  KW("\005", r_eax,  (OP_CLASS_R << 6) + (R_ENCODE_STYLE_IMPLICIT  << 4) + SIZE_CLASS_GPR_0) \
  KW("\002", r8,     (OP_CLASS_R << 6) + (R_ENCODE_STYLE_MODRM_REG << 4) + 0) \
  KW("\006", r8_ecx, (OP_CLASS_R << 6) + (R_ENCODE_STYLE_IMPLICIT  << 4) + SIZE_CLASS_CL) \
  KW("\003", r16,    (OP_CLASS_R << 6) + (R_ENCODE_STYLE_MODRM_REG << 4) + 1) \
  KW("\003", r32,    (OP_CLASS_R << 6) + (R_ENCODE_STYLE_MODRM_REG << 4) + 2) \
  KW("\003", r64,    (OP_CLASS_R << 6) + (R_ENCODE_STYLE_MODRM_REG << 4) + 3) \
  KW("\001", x,      (OP_CLASS_R << 6) + (R_ENCODE_STYLE_MODRM_REG << 4) + 4) \
  KW("\001", m,      (OP_CLASS_M << 6) + (M_ENCODE_STYLE_MODRM_RM  << 4) + SIZE_CLASS_GPR) \
  KW("\002", m8,     (OP_CLASS_M << 6) + (M_ENCODE_STYLE_MODRM_RM  << 4) + 0) \
  KW("\003", m16,    (OP_CLASS_M << 6) + (M_ENCODE_STYLE_MODRM_RM  << 4) + 1) \
  KW("\003", m32,    (OP_CLASS_M << 6) + (M_ENCODE_STYLE_MODRM_RM  << 4) + 2) \
  KW("\003", m64,    (OP_CLASS_M << 6) + (M_ENCODE_STYLE_MODRM_RM  << 4) + 3) \
  KW("\002", rm,     (OP_CLASS_RM << 6) + SIZE_CLASS_GPR) \
  KW("\003", rm8,    (OP_CLASS_RM << 6) + 0) \
  KW("\004", rm16,   (OP_CLASS_RM << 6) + 1) \
  KW("\004", rm32,   (OP_CLASS_RM << 6) + 2) \
  KW("\004", rm64,   (OP_CLASS_RM << 6) + 3) \
  KW("\005", xm128,  (OP_CLASS_RM << 6) + 4) \
  KW("\001", i,      (OP_CLASS_IMM << 6) + IMM_SIZE_CLASS_INFERRED_8_16_32) \
  KW("\002", i8,     (OP_CLASS_IMM << 6) + IMM_SIZE_CLASS_I8) \
  KW("\004", i8_1,   (OP_CLASS_IMM << 6) + IMM_SIZE_CLASS_CONSTANT_1) \
  KW("\003", i16,    (OP_CLASS_IMM << 6) + IMM_SIZE_CLASS_I16) \
  KW("\003", i32,    (OP_CLASS_IMM << 6) + IMM_SIZE_CLASS_I32) \
  KW("\003", i64,    (OP_CLASS_IMM << 6) + IMM_SIZE_CLASS_I64) /* Must be last operand. */\
  /* Special instructions; and must be first, xor must be last. */\
  KW("\003", and,  FUNGE_and) \
  KW("\003", lea,  FUNGE_lea) \
  KW("\003", mov,  FUNGE_mov) \
  KW("\004", test, FUNGE_test) \
  KW("\003", xor,  FUNGE_xor) \
  /* Special encoding actions; rex must be first, rel_call must be last. */\
  KW("\003", rex, 0) \
  KW("\007", rel_jmp,  RELOC_jmp) \
  KW("\007", rel_jcc,  RELOC_jcc) \
  KW("\010", rel_call, RELOC_call) \
  /* Misc other keywords. */\
  KW("\004", no_8, 0) \
  KW("\007", special, 0) \
  KW("\001", w, 0)
#include "../sht_keywords.h"

static void emit_chars(FILE* out, const char* str, size_t n) {
  while (n--) {
    fprintf(out, "'%c', ", *str++);
  }
}

static void emit_bytes(FILE* out, const uint8_t* buf, size_t n) {
  while (n--) {
    fprintf(out, "0x%02x, ", (unsigned)*buf++);
  }
}

void build_operand_keywords(FILE* out) {
#define put(s, v) fprintf(out, "\\%03o\\%03o%s", (int)strlen(s), (int)(v ^ OP_SPECIAL_NOTHING), s);
  const char* names[8] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
  char buf[8];
  fprintf(out, "static const char operand_keywords[] = \"");
  for (int i = 0; i < 8; ++i) {
    sprintf(buf, "r%s", names[i]);
    put(buf, PACK_OP(3, i));
    buf[0] = 'e';
    put(buf, PACK_OP(2, i));
    put(buf + 1, PACK_OP(1, i));
    sprintf(buf, "st%d", i);
    put(buf, PACK_SPECIAL(SPECIAL_KIND_ST_REG, i));
    sprintf(buf, "k%d", i);
    put(buf, PACK_SPECIAL(SPECIAL_KIND_K_REG, i));
  }
  for (int i = 0; i < 4; ++i) {
    sprintf(buf, "%cl", names[i][0]);
    put(buf, PACK_OP(0, i));
    buf[1] = 'h';
    put(buf, PACK_SPECIAL(SPECIAL_KIND_H_REG, i));
    sprintf(buf, "%sl", names[4 + i]);
    put(buf, PACK_OP(0, 4 + i));
  }
  for (int i = 0; i < 16; ++i) {
    sprintf(buf, "r%d", i);
    put(buf, PACK_OP(3, i));
    sprintf(buf, "r%dd", i);
    put(buf, PACK_OP(2, i));
    sprintf(buf, "r%dw", i);
    put(buf, PACK_OP(1, i));
    sprintf(buf, "r%db", i);
    put(buf, PACK_OP(0, i));
    sprintf(buf, "xmm%d", i);
    put(buf, PACK_OP(4, i));
    sprintf(buf, "ymm%d", i);
    put(buf, PACK_OP(5, i));
    sprintf(buf, "zmm%d", i);
    put(buf, PACK_OP(6, i));
  }
  put("ptr", OP_SPECIAL_KW_PTR);
  put("byte", PACK_SPECIAL(SPECIAL_KIND_SIZE_KW, 0));
  put("word", PACK_SPECIAL(SPECIAL_KIND_SIZE_KW, 1));
  put("dword", PACK_SPECIAL(SPECIAL_KIND_SIZE_KW, 2));
  put("qword", PACK_SPECIAL(SPECIAL_KIND_SIZE_KW, 3));
  put("xword", PACK_SPECIAL(SPECIAL_KIND_SIZE_KW, 4));
  put("yword", PACK_SPECIAL(SPECIAL_KIND_SIZE_KW, 5));
  put("zword", PACK_SPECIAL(SPECIAL_KIND_SIZE_KW, 6));
  put("tword", PACK_SPECIAL(SPECIAL_KIND_SIZE_KW, 7));
#undef put
  fprintf(out, "\";\n");
}

static void encode_reg_as_plus_r(uint8_t* ops, uint32_t n) {
  uint32_t affected = 0;
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t info = ops[i];
    if ((info >> 4) == ((OP_CLASS_R << 2) + R_ENCODE_STYLE_MODRM_REG)) {
      info ^= (R_ENCODE_STYLE_MODRM_REG << 4) ^ (R_ENCODE_STYLE_ADD_TO_OPCODE << 4);
      ops[i] = info;
      ++affected;
    }
  }
  if (affected != 1) FATAL("bad +r");
}

static void encode_m_as_reloc(uint8_t* ops, uint32_t n, uint32_t reloc) {
  uint32_t affected = 0;
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t info = ops[i];
    if (info == (OP_CLASS_M << 6) + (M_ENCODE_STYLE_MODRM_RM << 4) + SIZE_CLASS_GPR) {
      info = (OP_CLASS_M << 6) + (M_ENCODE_STYLE_RELOC << 4) + reloc;
      ops[i] = info;
      ++affected;
    }
  }
  if (affected != 1) FATAL("bad reloc");
}

static void apply_no8(uint8_t* ops, uint32_t n) {
  uint32_t affected = 0;
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t info = ops[i];
    if ((info & 0xf) == SIZE_CLASS_GPR && (info >> 6) != OP_CLASS_IMM) {
      info ^= SIZE_CLASS_GPR ^ SIZE_CLASS_GPR_NO8;
      ops[i] = info;
      ++affected;
    }
  }
  if (affected < 1) FATAL("bad no8");
}

static void check_encoding(uint8_t* ops, uint32_t n, uint64_t slash_n) {
  uint32_t num_add_to_opcode = 0;
  uint32_t num_modrm_r = 0;
  uint32_t num_modrm_m = 0;
  uint32_t num_m = 0;
  uint32_t num_v = 0;
  uint32_t num_kmask = 0;
  uint32_t num_imm = 0;
  if (slash_n) num_modrm_r += 1;

  for (uint32_t i = 0; i < n; ++i) {
    uint32_t info = ops[i];
    switch (info >> 4) {
    case (OP_CLASS_R << 2) + R_ENCODE_STYLE_MODRM_REG:
      num_modrm_r += 1;
      break;
    case (OP_CLASS_R << 2) + R_ENCODE_STYLE_ADD_TO_OPCODE:
      if (num_add_to_opcode) FATAL("too many +r");
      num_add_to_opcode += 1;
      break;
    case (OP_CLASS_R << 2) + R_ENCODE_STYLE_V:
      if (num_v) FATAL("too many v");
      num_v += 1;
      break;
    case (OP_CLASS_R << 2) + R_ENCODE_STYLE_IMPLICIT:
      break;
    case (OP_CLASS_R << 2) + R_ENCODE_STYLE_MODRM_RM:
    case (OP_CLASS_RM << 2) + 0:
    case (OP_CLASS_M << 2) + M_ENCODE_STYLE_MODRM_RM:
      num_modrm_m += 1;
      num_m += 1;
      break;
    case (OP_CLASS_R << 2) + R_ENCODE_STYLE_KMASK:
      if (num_kmask) FATAL("too many k");
      num_kmask += 1;
      break;
    case (OP_CLASS_R << 2) + R_ENCODE_STYLE_IMM4:
      num_imm += 1;
      break;
    case (OP_CLASS_M << 2) + M_ENCODE_STYLE_IMPLICIT_RDI:
    case (OP_CLASS_M << 2) + M_ENCODE_STYLE_IMPLICIT_RSI:
      num_m += 1;
      break;
    case (OP_CLASS_M << 2) + M_ENCODE_STYLE_RELOC:
      if (n != 1) FATAL("bad reloc");
      num_m += 1;
      break;
    case (OP_CLASS_IMM << 2) + 0:
      num_imm += 1;
      break;
    default:
      FATAL("Bad class/encoding combination");
    }
  }
  if (num_modrm_r | num_modrm_m) {
    if (num_modrm_r != 1 || num_modrm_m != 1) FATAL("bad modrm encoding");
    if (num_add_to_opcode) FATAL("cannot have both +r and modrm, as both use rex.b");
  }
  if (num_imm > 1) FATAL("too many imm");
  if (num_m > 1) FATAL("too many mem");
}

typedef struct insn_info_t {
  uint8_t overload_heads[16];
  uint8_t overload_tails[16];
  uint16_t used;
  uint8_t special;
  uint8_t buf[256 + 32];
} insn_info_t;

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
#define ENTER_KW_DATA(_, name, data) *(uint32_t*)sht_u_to_p(&toks.idents, KW_##name) = data;
KWS(ENTER_KW_DATA)
#undef ENTER_KW_DATA
  tokeniser_load_file(&toks, argv[1]);
  tokeniser_run(&toks);
  token_t* t = toks.tokens;
  insn_info_t insn_info;
  memset(&insn_info, 0, sizeof(insn_info));
  fprintf(out, "static const uint8_t insn_encodings[] = {\n");
  for (;;) {
    // insn_name
    uint32_t insn_name = t->type;
    if (insn_name >= TOK_IDENT_THR) {
      if (t->type == TOK_EOF) break;
      tokeniser_error(&toks, t, "expected identifier");
    }
    ++t;
  
    // Operands and constraints
    uint32_t seen_no8 = 0;
    uint32_t this_overload_start = insn_info.used + 1;
    uint8_t* dst = insn_info.buf + this_overload_start;
    dst[-1] = 0;
    for (;;) {
      if (t->type <= KW_i64) {
        *dst++ = *(uint32_t*)sht_u_to_p(&toks.idents, t->type) & 0xff;
        ++t;
      } else if (t->type == TOK_PUNCT('(')) {
        switch (t[1].type) {
        case KW_no_8: seen_no8 |= 1; break;
        case KW_special: insn_info.special |= 1; break;
        default: tokeniser_error(&toks, t, "bad constraint"); break;
        }
        if (t[2].type != TOK_PUNCT(')')) {
          tokeniser_error(&toks, t + 2, "expected )");
        }
        t += 3;
      } else if (t->type == (TOK_PUNCT(':') + PUNCT_REPEAT)) {
        ++t;
        break;
      } else {
        tokeniser_error(&toks, t, "expected operand");
      }
    }
    uint32_t num_operands = dst - (insn_info.buf + this_overload_start);
  
    // Encoding
    uint8_t num_opcode_bytes = 0;
    uint8_t rex_w = 0;
    uint8_t plus_r = 0;
    uint64_t slash_n = 0;
    uint32_t reloc = 0;
    *dst++ = 0; // will be filled in later (via dst[-1-num_opcode_bytes])
    for (;;) {
      uint32_t tt = t->type;
      if (t->start + 2 == t->end && tokeniser_is_hex_range(&toks, t->start, t->end)) {
        *dst++ = tokeniser_decode_hex_range(&toks, t->start, t->end);
        ++num_opcode_bytes;
        ++t;
        continue;
      } else if (KW_rex <= tt && tt <= KW_rel_call) {
        if (tt == KW_rex) {
          if (t[1].type != TOK_PUNCT('.') || t[2].type != KW_w) {
            tokeniser_error(&toks, t + 1, "expected .w after rex");
          }
          t += 3;
          rex_w |= 1;
          continue;
        } else {
          reloc = tt;
          ++t;
          continue;
        }
      } else if (tt == TOK_PUNCT('+')) {
        if (t[1].type != KW_r) {
          tokeniser_error(&toks, t + 1, "expected r after +");
        }
        t += 2;
        plus_r |= 1;
        continue;
      } else if (tt == TOK_PUNCT('/')) {
        if (t[1].type != TOK_NUMBER || t[1].start + 1 != t[1].end) {
          tokeniser_error(&toks, t + 1, "expected number after /");
        }
        tokeniser_decode_uint(&toks, t+1, &slash_n);
        slash_n = 0x40 | ((slash_n & 7) << 3);
        t += 2;
        continue;
      } else {
        break;
      }
    }
    // Finalise encoding.
    if (reloc) {
      reloc = *(uint32_t*)sht_u_to_p(&toks.idents, reloc) & 0xff;
      encode_m_as_reloc(insn_info.buf + this_overload_start, num_operands, reloc);
    }
    if (seen_no8) {
      apply_no8(insn_info.buf + this_overload_start, num_operands);
    }
    if (plus_r) {
      encode_reg_as_plus_r(insn_info.buf + this_overload_start, num_operands);
    }
    check_encoding(insn_info.buf + this_overload_start, num_operands, slash_n);
    dst[-1-num_opcode_bytes] = (rex_w ? ENC_FORCE_REX_W : 0) | (slash_n ? ENC_HAS_MODRM_NIBBLE : 0) | num_opcode_bytes;
    if (slash_n) *dst++ = slash_n;
    insn_info.used = dst - insn_info.buf;
    if (insn_info.used >= 0xff) FATAL("Encoding info too long!");
    // Attach this overload to the appropriate per-length chain.
    uint32_t tail_pos = insn_info.overload_tails[num_operands];
    if (tail_pos) {
      insn_info.buf[tail_pos - 1] = this_overload_start - tail_pos;
    } else {
      insn_info.overload_heads[num_operands] = this_overload_start;
    }
    insn_info.overload_tails[num_operands] = this_overload_start;

    if (t->type != insn_name) {
      uint32_t pre_buf_bytes = (insn_info.special ? 1 : 0);
      uint32_t min_n_operands = 0xff, max_n_operands = 0;
      for (uint32_t i = 0; i < sizeof(insn_info.overload_heads); ++i) {
        if (insn_info.overload_heads[i] != 0) {
          if (i < min_n_operands) min_n_operands = i;
          if (i > max_n_operands) max_n_operands = i;
        }
      }
      if (max_n_operands) {
        pre_buf_bytes += 1;
      }
      if (min_n_operands != max_n_operands) {
        pre_buf_bytes += (max_n_operands - min_n_operands + 1);
      }
      uint32_t* insn_name_p = sht_u_to_p(&toks.idents, insn_name);
      const char* insn_name_str = sht_p_key(insn_name_p);
      if (*insn_name_p & 0x80000000) {
        FATAL("overloads for %s are not contiguous", insn_name_str);
      }
      *insn_name_p |= 0x80000000;
      uint32_t insn_name_len = strlen(insn_name_str);
      fprintf(out, "%d, %d, ", insn_name_len, pre_buf_bytes + insn_info.used);
      emit_chars(out, insn_name_str, insn_name_len);
      if (max_n_operands) {
        fprintf(out, "0x%02x, ", (int)(min_n_operands | (max_n_operands << 4) | (insn_info.special ? 8 : 0)));
      }
      if (insn_info.special) {
        if (!(KW_and <= insn_name && insn_name <= KW_xor)) {
          FATAL("do not know how to make %s special", insn_name_str);
        }
        insn_info.special = *(uint32_t*)sht_u_to_p(&toks.idents, insn_name) & 0xff;
        fprintf(out, "0x%02x, ", insn_info.special);
      }
      if (min_n_operands != max_n_operands) {
        for (uint32_t i = min_n_operands; i <= max_n_operands; ++i) {
          uint32_t head = insn_info.overload_heads[i];
          if (head) head += (max_n_operands - min_n_operands + 1);
          fprintf(out, "0x%02x, ", (int)head);
        }
      }
      emit_bytes(out, insn_info.buf, insn_info.used);
      fprintf(out, "\n");
      memset(&insn_info, 0, sizeof(insn_info));
    }
  }
  fprintf(out, "0, 0};\n");
  tokeniser_free(&toks);

  build_operand_keywords(out);
  fclose(out);
  return 0;
}
