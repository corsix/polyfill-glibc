#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ENCODING_SIZE_CONSTRAINT 0x10

#define ASM_SYM_FLAG_PUBLIC 0x01
#define ASM_SYM_FLAG_EXTERN 0x02
#define ASM_SYM_FLAG_FUNCTION 0x04
#define ASM_SYM_FLAG_CONST 0x08
#define ASM_SYM_FLAG_INIT 0x10
#define ASM_SYM_FLAG_FINI 0x20

typedef enum operand_class_other_t {
  operand_class_other_rel16,
  operand_class_other_rel21,
  operand_class_other_rel28,
  operand_class_other_rel21a,
  operand_class_other_rel21ap,
  operand_class_other_rel21l,
  operand_class_other_addr1,
  operand_class_other_addr2,
  operand_class_other_addr4,
  operand_class_other_addr8,
  operand_class_other_addra,
  operand_class_other_addrp,
  operand_class_other_cc,
  operand_class_other_ccb,
  operand_class_other_jc,
  operand_class_other_phantom_ref,
  operand_class_other_rel32,
} operand_class_other_t;

#define S_OK_64(x, b) (((((uint64_t)(x)) + (1ull << (b-1))) >> (b)) == 0)
#define S_OK_32(x, b) (((((uint32_t)(x)) + (1u << (b-1))) >> (b)) == 0)
#define S_PACK(x, b) ((x) & ((1ull << (b)) - 1))

static inline bool encode_reloc(operand_class_other_t kind, uint8_t* where, int64_t delta) {
  uint32_t bits;
  memcpy(&bits, where, sizeof(uint32_t));
  switch (kind) {
  case operand_class_other_rel16:
    if (delta & 3) return false;
    if (!S_OK_64(delta >>= 2, 14)) return false;
    bits ^= S_PACK(delta, 14) << 5;
    break;
  case operand_class_other_rel21:
  case operand_class_other_rel21l:
    if (delta & 3) return false;
    if (!S_OK_64(delta >>= 2, 19)) return false;
    bits ^= S_PACK(delta, 19) << 5;
    break;
  case operand_class_other_rel28:
    if (delta & 3) return false;
    if (!S_OK_64(delta >>= 2, 26)) return false;
    bits ^= S_PACK(delta, 26);
    break;
  case operand_class_other_rel21a:
    if (!S_OK_64(delta, 21)) return false;
    bits ^= (S_PACK((delta & ~(int64_t)3) >> 2, 19) << 5) ^ ((delta & 3) << 29);
    break;
  case operand_class_other_rel32:
    if (!S_OK_64(delta, 32)) return false;
    bits ^= S_PACK(delta, 32);
    break;
  default:
    return false;
  }
  memcpy(where, &bits, sizeof(uint32_t));
  return true;
}
