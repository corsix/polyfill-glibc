#include "../common.h"
#include "../erw.h"
#include "../../build/x86_64/relative_interp_payload.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>

typedef struct mini_erw_state_t {
  uint64_t f_size;
  int fd;
  uint8_t file_kind;
  const char* filename;
  void* header;
} mini_erw_state_t;

static void writev_all(mini_erw_state_t* erw, struct iovec* iovs, int num_iov) {
  while (num_iov) {
    ssize_t n = writev(erw->fd, iovs, num_iov);
    if (n > 0) {
      while (num_iov) {
        if ((size_t)n >= iovs->iov_len) {
          n -= iovs->iov_len;
          ++iovs;
          --num_iov;
        } else {
          iovs->iov_base = (char*)iovs->iov_base + n;
          iovs->iov_len -= n;
          break;
        }
      }
    } else if (errno != EINTR) {
      FATAL("Could not append to %s", erw->filename);
    }
  }
}

#define NNE_INSTANTIATE "x86_64/set_relative_interp_nne.h"
#include "../nne_instantiator.h"

static void classify_file(mini_erw_state_t* erw) {
  struct Elf32_Ehdr* hdr32 = (struct Elf32_Ehdr*)erw->header;
  if (erw->f_size < sizeof(*hdr32) || memcmp(hdr32->e_ident, "\177ELF", 4) != 0) {
    FATAL("%s is not an ELF file", erw->filename);
  }

  erw->file_kind = 0;
  uint16_t type = hdr32->e_type;
  if (type != ET_EXEC && type != ET_DYN) {
    type = __builtin_bswap16(type);
    if (type == ET_EXEC || type == ET_DYN) {
      // ok, need bswap
      erw->file_kind |= ERW_FILE_KIND_BSWAP;
    } else { invalid:
      FATAL("%s is not a supported type of ELF file", erw->filename);
    }
  }

  uint8_t clazz = hdr32->e_ident[4];
  if (clazz != 2) {
    uint16_t e_phentsize = hdr32->e_phentsize;
    if (erw->file_kind & ERW_FILE_KIND_BSWAP) e_phentsize = __builtin_bswap16(e_phentsize);
    if (e_phentsize == sizeof(struct Elf32_Phdr)) {
      return;
    }
  }
  if (erw->f_size >= sizeof(struct Elf64_Ehdr)) {
    uint16_t e_phentsize = ((struct Elf64_Ehdr*)hdr32)->e_phentsize;
    if (erw->file_kind & ERW_FILE_KIND_BSWAP) e_phentsize = __builtin_bswap16(e_phentsize);
    if (e_phentsize == sizeof(struct Elf64_Phdr)) {
      erw->file_kind |= ERW_FILE_KIND_64;
      return;
    }
  }
  if (clazz == 2) {
    uint16_t e_phentsize = hdr32->e_phentsize;
    if (erw->file_kind & ERW_FILE_KIND_BSWAP) e_phentsize = __builtin_bswap16(e_phentsize);
    if (e_phentsize == sizeof(struct Elf32_Phdr)) {
      return;
    }
  }
  goto invalid;
}

static bool remove_prefix(const char** str_p, const char* prefix) {
  const char* str = *str_p;
  if (!has_prefix(str, prefix)) return false;
  str += strlen(prefix);
  while (*str == '/') ++str;
  *str_p = str;
  return true;
}

int main(int argc, const char** argv) {
  if (argc < 3) {
    FATAL("Usage: %s executable-to-modify relative/path/to/ld-linux-x86-64.so.2", argv[0]);
  }

  // Open the file.
  mini_erw_state_t erw;
  erw.filename = argv[1];
  erw.fd = open(erw.filename, O_RDWR | O_APPEND | O_CLOEXEC);
  if (erw.fd < 0) FATAL("Could not open file %s", erw.filename);
  struct stat st;
  if (fstat(erw.fd, &st) != 0) FATAL("Could not stat file %s", erw.filename);
  erw.f_size = st.st_size;
  erw.header = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, erw.fd, 0);
  if (erw.header == MAP_FAILED) FATAL("Could not mmap file %s", erw.filename);
  classify_file(&erw);

  // Do what we're here to do.
  const char* interp_path = argv[2];
  (void)(remove_prefix(&interp_path, "$ORIGIN/") || remove_prefix(&interp_path, "${ORIGIN}/"));
  (void)remove_prefix(&interp_path, "./");
  call_erwNNE_(set_relative_interp, &erw, interp_path);

  // Cleanup.
  munmap(erw.header, 4096);
  close(erw.fd);
  return 0;
}
