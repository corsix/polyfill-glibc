// We really want a C++ template system for editing ELF files, as they come
// in four flavours: 32-bit native endian, 32-bit non-native endian, 64-bit
// native endian, 64-bit non-native endian. In the absence of such a template
// system, nne_instantiator.h will #include NNE_INSTANTIATE four times, once
// for each flavour, and then #undef NNE_INSTANTIATE.

#define ERW_NATIVE_ENDIAN 1
#define Elf_bswapu16(x) ((uint16_t)(x))
#define Elf_bswapu32(x) ((uint32_t)(x))
#define Elf_bswapu64(x) ((uint64_t)(x))
#define erwE_(x) erw_n_##x

#define ERW_32 1
#define Elf_bswapuNN(x) ((uint32_t)(x))
#define Elf_iNN int32_t
#define Elf_uNN uint32_t
#define ElfNN_(x) Elf32_##x
#define erwNN_(x) erw32_##x
#define erwNNE_(x) erw32n_##x
#include NNE_INSTANTIATE
#undef ERW_32
#undef Elf_bswapuNN
#undef Elf_iNN
#undef Elf_uNN
#undef ElfNN_
#undef erwNN_
#undef erwNNE_

#define ERW_64 1
#define Elf_bswapuNN(x) ((uint64_t)(x))
#define Elf_iNN int64_t
#define Elf_uNN uint64_t
#define ElfNN_(x) Elf64_##x
#define erwNN_(x) erw64_##x
#define erwNNE_(x) erw64n_##x
#include NNE_INSTANTIATE
#undef ERW_64
#undef Elf_bswapuNN
#undef Elf_iNN
#undef Elf_uNN
#undef ElfNN_
#undef erwNN_
#undef erwNNE_

#undef ERW_NATIVE_ENDIAN
#undef Elf_bswapu16
#undef Elf_bswapu32
#undef Elf_bswapu64
#undef erwE_

#define ERW_SWAP_ENDIAN 1
#define Elf_bswapu16(x) (__builtin_bswap16((x)))
#define Elf_bswapu32(x) (__builtin_bswap32((x)))
#define Elf_bswapu64(x) (__builtin_bswap64((x)))
#define erwE_(x) erw_s_##x

#define ERW_32 1
#define Elf_bswapuNN(x) (__builtin_bswap32((x)))
#define Elf_iNN int32_t
#define Elf_uNN uint32_t
#define ElfNN_(x) Elf32_##x
#define erwNN_(x) erw32_##x
#define erwNNE_(x) erw32s_##x
#include NNE_INSTANTIATE
#undef ERW_32
#undef Elf_bswapuNN
#undef Elf_iNN
#undef Elf_uNN
#undef ElfNN_
#undef erwNN_
#undef erwNNE_

#define ERW_64 1
#define Elf_bswapuNN(x) (__builtin_bswap64((x)))
#define Elf_iNN int64_t
#define Elf_uNN uint64_t
#define ElfNN_(x) Elf64_##x
#define erwNN_(x) erw64_##x
#define erwNNE_(x) erw64s_##x
#include NNE_INSTANTIATE
#undef ERW_64
#undef Elf_bswapuNN
#undef Elf_iNN
#undef Elf_uNN
#undef ElfNN_
#undef erwNN_
#undef erwNNE_

#undef ERW_SWAP_ENDIAN
#undef elf_bswap16
#undef elf_bswap32
#undef elf_bswap64
#undef erwE_

#undef NNE_INSTANTIATE
