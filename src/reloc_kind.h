#pragma once
#include <stdint.h>

enum reloc_kind {
  reloc_kind_noop,
  reloc_kind_no_symbol_lookup, /* Must be last kind that doesn't do symbol lookup. */
  reloc_kind_lookup_result_unused,
  reloc_kind_regular,
  reloc_kind_class_plt,
  reloc_kind_class_copy,
};

/*
Input is low 32 bits of r_info of an ELF relocation.
Output is the general category of that relocation.
*/
typedef enum reloc_kind (*reloc_kind_classifier)(uint32_t);

/*
Input is e_machine field from ELF file header (translated to host endianness).
*/
reloc_kind_classifier reloc_kind_classifier_for_machine(uint16_t machine);
