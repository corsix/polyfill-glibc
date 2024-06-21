#include <dlfcn.h>
#include <errno.h>
#include "../../src/common.h"
#include "../syscall_filter.h"

static void test_getentropy(void* lib, bool good) {
  int* err = NULL;
  int (*fn)(void*, size_t);
  fn = dlvsym(lib, "getentropy", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "getentropy", "GLIBC_2.25");
    if (!fn) {
      FATAL("Could not find getentropy");
    }
    err = dlsym(lib, "errno");
  }
  if (!err) err = &errno;

  uint8_t buf[300] = {0};
  *err = 0;
  ASSERT(fn(buf, 300) == -1);
  ASSERT(*err == EIO || *err == ENOSYS);
  int r = fn(buf, 200);
  if (r == -1 && *err == ENOSYS) return;
  ASSERT(good);
  ASSERT(r == 0);
  uint32_t seen = 0;
  for (uint32_t i = 0; i < 200; ++i) {
    seen |= 1u << (buf[i] & 31);
  }
  uint32_t distinct = 0;
  while (seen) {
    ++distinct;
    seen &= seen - 1;
  }
  ASSERT(distinct >= 3);
}

static void test_getrandom(void* lib, bool good) {
  int* err = NULL;
  ssize_t (*fn)(void*, size_t, unsigned int);
  fn = dlvsym(lib, "getrandom", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "getrandom", "GLIBC_2.25");
    if (!fn) {
      FATAL("Could not find getrandom");
    }
    err = dlsym(lib, "errno");
  }
  if (!err) err = &errno;

  uint8_t buf[300] = {0};
  *err = 0;
  ASSERT(fn((void*)1, 1, 0) == -1);
  ASSERT(*err == EFAULT || *err == ENOSYS);
  ssize_t r = fn(buf, 300, 0);
  if (r == -1 && *err == ENOSYS) return;
  ASSERT(good);
  ASSERT(r >= 0);
  ASSERT(r <= 300);
  uint32_t seen = 0;
  for (ssize_t i = 0; i < r; ++i) {
    seen |= 1u << (buf[i] & 31);
  }
  uint32_t distinct = 0;
  while (seen) {
    ++distinct;
    seen &= seen - 1;
  }
  if (r > 100) {
    ASSERT(distinct >= 3);
  } else if (r > 10) {
    ASSERT(distinct >= 2);
  }
}

static void test_arc4random(void* lib) {
  uint32_t (*fn)(void);
  fn = dlvsym(lib, "arc4random", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "arc4random", "GLIBC_2.36");
    if (!fn) {
      FATAL("Could not find arc4random");
    }
  }
  uint32_t seen1 = 0;
  uint32_t seen0 = 0;
  for (uint32_t i = 0;; ++i) {
    uint32_t val = fn();
    seen1 |= val;
    seen0 |= ~val;
    if (!~(seen0 & seen1)) break;
    ASSERT(i < 1000);
  }
}

static void test_arc4random_uniform(void* lib) {
  uint32_t (*fn)(uint32_t);
  fn = dlvsym(lib, "arc4random_uniform", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "arc4random_uniform", "GLIBC_2.36");
    if (!fn) {
      FATAL("Could not find arc4random_uniform");
    }
  }
  uint32_t seen = 0;
  for (uint32_t i = 0;; ++i) {
    uint32_t val = fn(17);
    ASSERT(val < 17);
    seen |= (1 << val);
    if (i > 100) {
      if (seen == 0x1ffff) break;
      ASSERT(i < 100000);
    }
  }
}

int main(int argc, const char** argv) {
  ASSERT(argc >= 2);
  void* lib = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
  if (!lib) FATAL("Could not dlopen %s", argv[1]);
  test_getentropy(lib, true);
  test_getrandom(lib, true);
  test_arc4random(lib);
  test_arc4random_uniform(lib);
#ifdef TEST_AARCH64
  set_one_syscall_filter(0x116, ENOSYS); // __NR_getrandom
#else
  set_one_syscall_filter(318, ENOSYS); // __NR_getrandom
#endif
  test_getentropy(lib, false);
  test_getrandom(lib, false);
  test_arc4random(lib);
  test_arc4random_uniform(lib);
  
  FILE* f = argv[2] ? fopen(argv[2], "w") : stdout;
  fputs("OK\n", f);
  fclose(f);
  return 0;
}
