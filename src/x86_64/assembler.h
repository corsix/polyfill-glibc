#pragma once

// Op kinds 0-6 are registers of width 1/2/4/8/16/32/64.
#define OP_KIND_SPECIAL 7
#define PACK_OP(kind, low) (((kind) << 5) | (low))

#define SPECIAL_KIND_ST_REG 0
#define SPECIAL_KIND_K_REG 1
#define SPECIAL_KIND_H_REG 2
#define SPECIAL_KIND_SIZE_KW 3

#define PACK_SPECIAL(kind, low) ((7 << 5) | ((kind) << 3) | (low))

#define OP_SPECIAL_LAST_REG PACK_SPECIAL(SPECIAL_KIND_H_REG, 3)
#define OP_SPECIAL_NOTHING PACK_SPECIAL(SPECIAL_KIND_H_REG, 4)
#define OP_SPECIAL_MEM PACK_SPECIAL(SPECIAL_KIND_H_REG, 5)
#define OP_SPECIAL_KW_PTR OP_SPECIAL_MEM // Re-using value.
#define OP_SPECIAL_IMM PACK_SPECIAL(SPECIAL_KIND_H_REG, 6)
#define OP_SPECIAL_SYMBOL PACK_SPECIAL(SPECIAL_KIND_H_REG, 7)
#define OP_SPECIAL_FIRST_SIZE_KW PACK_SPECIAL(SPECIAL_KIND_SIZE_KW, 0)

// For encoding operand constraints in a byte;
//  high two bits are OP_CLASS_*
//  middle two bits are encoding style
//  low four bits are SIZE_CLASS_*
#define OP_CLASS_R 0
#define OP_CLASS_RM 1
#define OP_CLASS_M 2
#define OP_CLASS_IMM 3

#define R_ENCODE_STYLE_MODRM_REG 0
#define R_ENCODE_STYLE_ADD_TO_OPCODE 1
#define R_ENCODE_STYLE_V 2
#define R_ENCODE_STYLE_IMPLICIT 3
// 4 used for class RM
#define R_ENCODE_STYLE_MODRM_RM 5
#define R_ENCODE_STYLE_KMASK 6
#define R_ENCODE_STYLE_IMM4 7

#define M_ENCODE_STYLE_MODRM_RM 0
#define M_ENCODE_STYLE_IMPLICIT_RSI 1
#define M_ENCODE_STYLE_IMPLICIT_RDI 2
#define M_ENCODE_STYLE_RELOC 3

#define ENC_FORCE_REX_W 0x80
#define ENC_HAS_MODRM_NIBBLE 0x40
#define ENC_LENGTH_MASK 0x3f

// Size class 0-6 means exactly 8/16/32/64/128/256/512-bit.
#define SIZE_CLASS_X87 7
#define SIZE_CLASS_K 8
#define SIZE_CLASS_CL 9 // cl
#define SIZE_CLASS_ST_0 10 // st0
#define SIZE_CLASS_XMM_0 11 // xmm0
#define SIZE_CLASS_GPR_0 12 // 8/16/32/64-bit, but must be al/ax/eax/rax, interacts with size inference
#define SIZE_CLASS_GPR 13 // 8/16/32/64-bit, interacts with size inference
#define SIZE_CLASS_GPR_NO8 14 // 16/32/64-bit, interacts with size inference
#define SIZE_CLASS_XYZ 15 // 128/256/512-bit, interacts with size inference

#define IMM_SIZE_CLASS_CONSTANT_1 0
#define IMM_SIZE_CLASS_I8 1
#define IMM_SIZE_CLASS_I16 2
#define IMM_SIZE_CLASS_I32 3
#define IMM_SIZE_CLASS_I64 4
#define IMM_SIZE_CLASS_INFERRED_8_16_32 5

#define FUNGE_and 1
#define FUNGE_lea 2
#define FUNGE_mov 3
#define FUNGE_test 4
#define FUNGE_xor 5

// 0-3 used for 32-bit RIP-rel memory operand, followed by 0/1/2/4 byte imm
#define RELOC_call 4
#define RELOC_jmp 5
#define RELOC_jcc 6
#define RELOC_def_label 7
#define RELOC_align 8
#define RELOC_u8_label_lut 9
#define RELOC_phantom_ref 10

#define RUN_RELOC_IMM_SIZE_MASK 0x03
#define RUN_RELOC_PHANTOM 0x04
#define RUN_RELOC_CALL_OR_JMP 0x08

#define ASM_SYM_FLAG_PUBLIC 0x01
#define ASM_SYM_FLAG_EXTERN 0x02
#define ASM_SYM_FLAG_FUNCTION 0x04
#define ASM_SYM_FLAG_CONST 0x08
#define ASM_SYM_FLAG_INIT 0x10
#define ASM_SYM_FLAG_FINI 0x20
