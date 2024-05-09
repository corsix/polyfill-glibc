#include "reloc_kind.h"
#include "elf.h"

enum reloc_kind reloc_kind_classifier_386(uint32_t type) {
  switch (type & 0xff) {
  case R_386_NONE:
    return reloc_kind_noop;
  case R_386_RELATIVE:
    return reloc_kind_no_symbol_lookup;
  case R_386_IRELATIVE:
    return reloc_kind_lookup_result_unused;
  case R_386_JMP_SLOT:
  case R_386_TLS_DTPMOD32:
  case R_386_TLS_DTPOFF32:
  case R_386_TLS_TPOFF32:
  case R_386_TLS_TPOFF:
  case R_386_TLS_DESC:
    return reloc_kind_class_plt;
  case R_386_COPY:
    return reloc_kind_class_copy;
  default:
    return reloc_kind_regular;
  }
}

enum reloc_kind reloc_kind_classifier_arm(uint32_t type) {
  switch (type & 0xff) {
  case R_ARM_NONE:
    return reloc_kind_noop;
  case R_ARM_RELATIVE:
    return reloc_kind_no_symbol_lookup;
  case R_ARM_IRELATIVE:
    return reloc_kind_lookup_result_unused;
  case R_ARM_JUMP_SLOT:
  case R_ARM_TLS_DTPMOD32:
  case R_ARM_TLS_DTPOFF32:
  case R_ARM_TLS_TPOFF32:
  case R_ARM_TLS_DESC:
    return reloc_kind_class_plt;
  case R_ARM_COPY:
    return reloc_kind_class_copy;
  default:
    return reloc_kind_regular;
  }
}

enum reloc_kind reloc_kind_classifier_x86_64(uint32_t type) {
  switch (type) {
  case R_X86_64_NONE:
    return reloc_kind_noop;
  case R_X86_64_RELATIVE:
  case R_X86_64_RELATIVE64:
    return reloc_kind_no_symbol_lookup;
  case R_X86_64_IRELATIVE:
    return reloc_kind_lookup_result_unused;
  case R_X86_64_JUMP_SLOT:
  case R_X86_64_DTPMOD64:
  case R_X86_64_DTPOFF64:
  case R_X86_64_TPOFF64:
  case R_X86_64_TLSDESC:
    return reloc_kind_class_plt;
  case R_X86_64_COPY:
    return reloc_kind_class_copy;
  default:
    return reloc_kind_regular;
  }
}

enum reloc_kind reloc_kind_classifier_aarch64(uint32_t type) {
  switch (type) {
  case R_AARCH64_NONE:
    return reloc_kind_noop;
  case R_AARCH64_P32_RELATIVE: case R_AARCH64_RELATIVE:
    return reloc_kind_no_symbol_lookup;
  case R_AARCH64_P32_IRELATIVE: case R_AARCH64_IRELATIVE:
    return reloc_kind_lookup_result_unused;
  case R_AARCH64_P32_JUMP_SLOT: case R_AARCH64_JUMP_SLOT:
  case R_AARCH64_P32_TLS_DTPMOD: case R_AARCH64_TLS_DTPMOD:
  case R_AARCH64_P32_TLS_DTPREL: case R_AARCH64_TLS_DTPREL:
  case R_AARCH64_P32_TLS_TPREL: case R_AARCH64_TLS_TPREL:
  case R_AARCH64_P32_TLSDESC: case R_AARCH64_TLSDESC:
    return reloc_kind_class_plt;
  case R_AARCH64_P32_COPY: case R_AARCH64_COPY:
    return reloc_kind_class_copy;
  default:
    return reloc_kind_regular;
  }
}

enum reloc_kind reloc_kind_classifier_fallback(uint32_t type) {
  (void)type;
  return reloc_kind_regular;
}

reloc_kind_classifier reloc_kind_classifier_for_machine(uint16_t machine) {
  switch (machine) {
  case EM_386: return reloc_kind_classifier_386;
  case EM_ARM: return reloc_kind_classifier_arm;
  case EM_X86_64: return reloc_kind_classifier_x86_64;
  case EM_AARCH64: return reloc_kind_classifier_aarch64;
  default: return reloc_kind_classifier_fallback;
  }
}
