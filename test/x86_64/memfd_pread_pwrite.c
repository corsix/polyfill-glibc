#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>
#include "../../src/common.h"
#include "../syscall_filter.h"

static void force_own_impl(void* fn) {
#ifdef TEST_AARCH64
  uint32_t* code = (uint32_t*)fn;
  uint32_t ldr = code[1];
  ASSERT((ldr >> 24) == 0x58);
  uint32_t br = code[3];
  ASSERT((br & 0xfffffc1f) == 0xd61f0000);
  int32_t delta = (int32_t)(ldr << 8) >> 13;
  *(void**)(code + 1 + delta) = code + 4;
#else
  uint8_t* code = (uint8_t*)fn;
  ASSERT(memcmp(code, "\xF3\x0F\x1E\xFA", 4) == 0); // endbr64
  code += 11;
  int32_t delta = ((int32_t*)code)[-1];
  void* own_impl = memmem(code, 32, "\xF3\x0F\x1E\xFA", 4); // endbr64
  ASSERT(own_impl);
  *(void**)(code + delta) = own_impl;
#endif
}

static void test_memfd_pread_pwrite(void* lib, bool force_own) {
  int (*memfd_create)(const char*, unsigned int);
  ssize_t (*preadv2)(int fd, const struct iovec *iov, int iovcnt, off_t offset, int flags);
  ssize_t (*pwritev2)(int fd, const struct iovec *iov, int iovcnt, off_t offset, int flags);
  int* err;
#define LOOKUP(sym, pfname, ver) \
  sym = dlvsym(lib, pfname, "POLYFILL"); \
  if (sym && force_own && (#sym)[0] == 'p') force_own_impl(sym); \
  if (!sym) sym = dlvsym(lib, #sym, ver); \
  if (!sym) FATAL("Could not find %s", #sym)
  LOOKUP(memfd_create, "memfd_create", "GLIBC_2.27");
  LOOKUP(preadv2, "preadv2_2_26", "GLIBC_2.26");
  LOOKUP(pwritev2, "pwritev2_2_26", "GLIBC_2.26");
  err = dlsym(lib, "errno");
  if (!err) err = &errno;
#undef LOOKUP
  *err = 0;
  int fd = memfd_create("dummy", 0xffff);
  ASSERT(fd == -1);
  ASSERT(*err == EINVAL);
  fd = memfd_create("dummy", 0);
  ASSERT(fd >= 0);

  ASSERT(write(fd, "AAAAA", 5) == 5);
  struct iovec iov;
  iov.iov_base = (void*)"DE";
  iov.iov_len = 2;
  ASSERT(pwritev2(fd, &iov, 1, 3, 0) == 2);
  iov.iov_base = (void*)"BC";
  iov.iov_len = 2;
  ASSERT(pwritev2(fd, &iov, 1, 1, 0) == 2);
  iov.iov_base = (void*)"FGH";
  iov.iov_len = 3;
  ASSERT(pwritev2(fd, &iov, 1, -1, 0) == 3);
  *err = 0;
  ASSERT(pwritev2(fd, &iov, 1, 0, 0xffff) == -1);
  ASSERT(*err == EOPNOTSUPP);

  ASSERT(lseek(fd, 0, SEEK_SET) == 0);
  char buf[4];
  ASSERT(read(fd, buf, 4) == 4 && memcmp(buf, "ABCD", 4) == 0);
  iov.iov_base = buf;
  iov.iov_len = 4;
  ASSERT(preadv2(fd, &iov, 1, 1, 0) == 4 && memcmp(buf, "BCDE", 4) == 0);
  ASSERT(preadv2(fd, &iov, 1, -1, 0) == 4 && memcmp(buf, "EFGH", 4) == 0);
  *err = 0;
  ASSERT(preadv2(fd, &iov, 1, 0, 0xffff) == -1);
  ASSERT(*err == EOPNOTSUPP);

  close(fd);
}

static int syscalls_to_block[] = {
  -1,
#ifdef TEST_AARCH64
  0x11e, // __NR_preadv2
  0x11f, // __NR_pwritev2
#else
  327, // __NR_preadv2
  328, // __NR_pwritev2
#endif
};

int main(int argc, const char** argv) {
  ASSERT(argc >= 2);
  void* lib = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
  if (!lib) FATAL("Could not dlopen %s", argv[1]);
  for (uint32_t force = 0; force < 2; ++force) {
    for (uint32_t i = 0; i < sizeof(syscalls_to_block)/sizeof(syscalls_to_block[0]); ++i) {
      int to_block = syscalls_to_block[i];
      if (to_block >= 0) set_one_syscall_filter(to_block, ENOSYS);
      test_memfd_pread_pwrite(lib, force != 0);
    }
  }
  
  FILE* f = argv[2] ? fopen(argv[2], "w") : stdout;
  fputs("OK\n", f);
  fclose(f);
  return 0;
}
