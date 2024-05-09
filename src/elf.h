// elf.h: Minimal definitions of ELF data structures.

#pragma once
#include <stdint.h>

struct Elf32_Ehdr {
  uint8_t  e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint32_t e_entry; // Wider in 64
  uint32_t e_phoff; // Wider in 64
  uint32_t e_shoff; // Wider in 64
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
};

struct Elf64_Ehdr {
  uint8_t  e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint64_t e_entry; // Narrower in 32
  uint64_t e_phoff; // Narrower in 32
  uint64_t e_shoff; // Narrower in 32
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
};

#define ET_EXEC 2
#define ET_DYN  3

#define EM_SPARC   2
#define EM_386     3
#define EM_ARM     40
#define EM_SPARCV9 43
#define EM_IA_64   50
#define EM_X86_64  62
#define EM_AARCH64 183
#define EM_TILEPRO 188
#define EM_TILEGX  191
#define EM_ALPHA   0x9026

struct Elf32_Sym {
  uint32_t st_name;
  uint32_t st_value; // Wider in 64
  uint32_t st_size;  // Wider in 64
  uint8_t  st_info;
  uint8_t  st_other;
  uint16_t st_shndx;
  // Note st_value and st_size here in 64
};

struct Elf64_Sym {
  uint32_t st_name;
  // Note st_value and st_size here in 32
  uint8_t  st_info;
  uint8_t  st_other;
  uint16_t st_shndx;
  uint64_t st_value; // Narrower in 32
  uint64_t st_size;  // Narrower in 32
};

struct Elf32_Rel {
  uint32_t r_offset; // Wider in 64
  uint32_t r_info;   // Wider in 64
};

struct Elf32_Rela {
  uint32_t r_offset; // Wider in 64
  uint32_t r_info;   // Wider in 64
  int32_t  r_addend; // Wider in 64
};

struct Elf64_Rel {
  uint64_t r_offset; // Narrower in 32
  uint64_t r_info;   // Narrower in 32
};

struct Elf64_Rela {
  uint64_t r_offset; // Narrower in 32
  uint64_t r_info;   // Narrower in 32
  int64_t  r_addend; // Narrower in 32
};

#define R_386_NONE         0
#define R_386_COPY         5
#define R_386_JMP_SLOT     7
#define R_386_RELATIVE     8
#define R_386_TLS_TPOFF    14
#define R_386_TLS_DTPMOD32 35
#define R_386_TLS_DTPOFF32 36
#define R_386_TLS_TPOFF32  37
#define R_386_TLS_DESC     41
#define R_386_IRELATIVE    42

#define R_ARM_NONE         0
#define R_ARM_TLS_DESC     13
#define R_ARM_TLS_DTPMOD32 17
#define R_ARM_TLS_DTPOFF32 18
#define R_ARM_TLS_TPOFF32  19
#define R_ARM_COPY         20
#define R_ARM_JUMP_SLOT    22
#define R_ARM_RELATIVE     23
#define R_ARM_IRELATIVE    160

#define R_X86_64_NONE       0
#define R_X86_64_COPY       5
#define R_X86_64_GLOB_DAT   6
#define R_X86_64_JUMP_SLOT  7
#define R_X86_64_RELATIVE   8
#define R_X86_64_DTPMOD64   16
#define R_X86_64_DTPOFF64   17
#define R_X86_64_TPOFF64    18
#define R_X86_64_TLSDESC    36
#define R_X86_64_IRELATIVE  37
#define R_X86_64_RELATIVE64 38

#define R_AARCH64_NONE           0
#define R_AARCH64_P32_COPY       180
#define R_AARCH64_P32_JUMP_SLOT  182
#define R_AARCH64_P32_RELATIVE   183
#define R_AARCH64_P32_TLS_DTPMOD 184
#define R_AARCH64_P32_TLS_DTPREL 185
#define R_AARCH64_P32_TLS_TPREL  186
#define R_AARCH64_P32_TLSDESC    187
#define R_AARCH64_P32_IRELATIVE  188
#define R_AARCH64_COPY           1024
#define R_AARCH64_JUMP_SLOT      1026
#define R_AARCH64_RELATIVE       1027
#define R_AARCH64_TLS_DTPMOD     1028
#define R_AARCH64_TLS_DTPREL     1029
#define R_AARCH64_TLS_TPREL      1030
#define R_AARCH64_TLSDESC        1031
#define R_AARCH64_IRELATIVE      1032

struct Elf32_Phdr {
  uint32_t p_type;
  // Note p_flags here in 64
  uint32_t p_offset; // Wider in 64
  uint32_t p_vaddr;  // Wider in 64
  uint32_t p_paddr;  // Wider in 64
  uint32_t p_filesz; // Wider in 64
  uint32_t p_memsz;  // Wider in 64
  uint32_t p_flags;
  uint32_t p_align;  // Wider in 64
};

struct Elf64_Phdr {
  uint32_t p_type;
  uint32_t p_flags;
  uint64_t p_offset; // Narrower in 32
  uint64_t p_vaddr;  // Narrower in 32
  uint64_t p_paddr;  // Narrower in 32
  uint64_t p_filesz; // Narrower in 32
  uint64_t p_memsz;  // Narrower in 32
  // Note p_flags here in 32
  uint64_t p_align;  // Narrower in 32
};

#define PT_NULL         0
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_PHDR         6
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK    0x6474e551
#define PT_GNU_RELRO    0x6474e552

#define PF_X 1
#define PF_W 2
#define PF_R 4

struct Elf32_Dyn {
  uint32_t d_tag;   // Wider in 64
  union {
    uint32_t d_val; // Wider in 64
    uint32_t d_ptr; // Wider in 64
  } d_un;
};

struct Elf64_Dyn {
  uint64_t d_tag;   // Narrower in 32
  union {
    uint64_t d_val; // Narrower in 32
    uint64_t d_ptr; // Narrower in 32
  } d_un;
};

#define DT_NULL           0
#define DT_NEEDED         1
#define DT_PLTRELSZ       2
#define DT_HASH           4
#define DT_STRTAB         5
#define DT_SYMTAB         6
#define DT_RELA           7
#define DT_RELASZ         8
#define DT_RELAENT        9
#define DT_STRSZ          10
#define DT_INIT           12
#define DT_FINI           13
#define DT_SONAME         14
#define DT_RPATH          15
#define DT_SYMBOLIC       16
#define DT_REL            17
#define DT_RELSZ          18
#define DT_RELENT         19
#define DT_PLTREL         20
#define DT_DEBUG          21
#define DT_TEXTREL        22
#define DT_JMPREL         23
#define DT_BIND_NOW       24
#define DT_RUNPATH        29
#define DT_FLAGS          30
#define DT_RELRSZ         35
#define DT_RELR           36
#define DT_RELRENT        37
#define DT_GNU_CONFLICTSZ 0x6ffffdf6
#define DT_GNU_LIBLISTSZ  0x6ffffdf7
#define DT_GNU_HASH       0x6ffffef5
#define DT_GNU_CONFLICT   0x6ffffef8
#define DT_GNU_LIBLIST    0x6ffffef9
#define DT_DEPAUDIT       0x6ffffefb
#define DT_AUDIT          0x6ffffefc
#define DT_VERSYM         0x6ffffff0
#define DT_FLAGS_1        0x6ffffffb
#define DT_VERDEF         0x6ffffffc
#define DT_VERDEFNUM      0x6ffffffd
#define DT_VERNEED        0x6ffffffe
#define DT_VERNEEDNUM     0x6fffffff
#define DT_X86_64_PLTENT  0x70000003
#define DT_AUXILIARY      0x7ffffffd
#define DT_FILTER         0x7fffffff

#define DF_SYMBOLIC 0x00000002
#define DF_TEXTREL  0x00000004
#define DF_BIND_NOW 0x00000008

#define DF_1_NOW    0x00000001

struct Elf_Verdef {
  uint16_t vd_version;
  uint16_t vd_flags;
  uint16_t vd_ndx;
  uint16_t vd_cnt;
  uint32_t vd_hash;
  uint32_t vd_aux;
  uint32_t vd_next;
};

#define VER_FLG_BASE 0x1
#define VER_FLG_WEAK 0x2

struct Elf_Verdaux {
  uint32_t vda_name;
  uint32_t vda_next;
};

struct Elf_Verneed {
  uint16_t vn_version;
  uint16_t vn_cnt;
  uint32_t vn_file;
  uint32_t vn_aux;
  uint32_t vn_next;
};

struct Elf_Vernaux {
  uint32_t vna_hash;
  uint16_t vna_flags;
  uint16_t vna_other;
  uint32_t vna_name;
  uint32_t vna_next;
};

struct Elf32_Shdr {
  uint32_t sh_name;
  uint32_t sh_type;
  uint32_t sh_flags;     // Wider in 64
  uint32_t sh_addr;      // Wider in 64
  uint32_t sh_offset;    // Wider in 64
  uint32_t sh_size;      // Wider in 64
  uint32_t sh_link;
  uint32_t sh_info;
  uint32_t sh_addralign; // Wider in 64
  uint32_t sh_entsize;   // Wider in 64
};

struct Elf64_Shdr {
  uint32_t sh_name;
  uint32_t sh_type;
  uint64_t sh_flags;     // Narrower in 32
  uint64_t sh_addr;      // Narrower in 32
  uint64_t sh_offset;    // Narrower in 32
  uint64_t sh_size;      // Narrower in 32
  uint32_t sh_link;
  uint32_t sh_info;
  uint64_t sh_addralign; // Narrower in 32
  uint64_t sh_entsize;   // Narrower in 32
};

#define SHN_UNDEF 0
#define SHN_ABS   0xfff1

#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_HASH        5
#define SHT_DYNAMIC     6
#define SHT_REL         9
#define SHT_DYNSYM      11
#define SHT_GNU_HASH    0x6ffffff6
#define SHT_GNU_verdef  0x6ffffffd
#define SHT_GNU_verneed 0x6ffffffe
#define SHT_GNU_versym  0x6fffffff

#define SHF_WRITE     0x01
#define SHF_ALLOC     0x02
#define SHF_EXECINSTR 0x04

#define STT_NOTYPE    0
#define STT_OBJECT    1
#define STT_FUNC      2
#define STT_SECTION   3
#define STT_FILE      4
#define STT_COMMON    5
#define STT_TLS       6
#define STT_GNU_IFUNC 10

#define STB_LOCAL      0
#define STB_GLOBAL     1
#define STB_WEAK       2
#define STB_GNU_UNIQUE 10

#define STV_INTERNAL 1
#define STV_HIDDEN   2
