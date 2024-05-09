#include <dlfcn.h>
#include <unwind.h>
#include "../../src/common.h"

static _Unwind_Reason_Code unwind_fn(struct _Unwind_Context* uc, void* ctx) {
  void* cfa = (void*)_Unwind_GetCFA(uc);
  uint8_t ctr = *(uint8_t*)ctx;
  ASSERT((ctr & 0x7f) != 0x7f);
  if (ctr & 0x80) {
    ASSERT((char*)cfa >= (char*)ctx);
    ctr += 1;
  } else if ((char*)cfa >= (char*)ctx) {
    ASSERT(ctr >= 2);
    ctr = 0x80;
  } else {
    ctr += 1;
  }
  *(uint8_t*)ctx = ctr;
  return _URC_NO_REASON;
}

static int rbit_cmp(const void* lhs, const void* rhs, void* ctx) {
  uint8_t* ctr = ctx;
  if (*ctr) {
    --*ctr;
  } else {
    // Time for an unwindability test!
    _Unwind_Backtrace(unwind_fn, ctr);
    ASSERT(*ctr >= 0x81);
    *ctr = 255;
    ctr[1] += 1;
  }

  uint32_t a = ~*(const uint8_t*)lhs;
  uint32_t b = ~*(const uint8_t*)rhs;
  if (a != b) {
    // See https://www.corsix.org/content/comparison-after-bit-reversal
    return ((a - b) & (b - a) & b) ? -1 : 1;
  }
  return 0;
}

static int masked_cmp(const void* lhs, const void* rhs, void* ctx) {
  uint32_t mask = *(const uint8_t*)ctx;
  uint32_t a = mask & *(const uint8_t*)lhs;
  uint32_t b = mask & *(const uint8_t*)rhs;
  return (int)a - (int)b;
}

static void test_qsort_r(void* lib) {
  void (*fn)(void* base, size_t nelem, size_t esz, int (*cmp)(const void*, const void*, void*), void* ctx);
  fn = dlvsym(lib, "qsort_r", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "qsort_r", "GLIBC_2.8");
    if (!fn) {
      FATAL("Could not find qsort_r");
    }
  }

  uint8_t ctr[2] = {0, 0};
  uint8_t buf[256];
  for (uint32_t i = 0; i < 256; ++i) {
    buf[i] = i;
  }
  for (uint32_t esz = 1; esz <= 80; ++esz) {
    uint32_t nelem = 256 / esz;
    fn(buf, nelem, esz, rbit_cmp, ctr);
    // Elements should be in the order defined by rbit_cmp.
    for (uint32_t i = (nelem - 1) * esz; i; ) {
      uint32_t j = i - esz;
      if (!ctr[0]) ctr[0] = 255; // Don't need unwindability testing right now.
      ASSERT(rbit_cmp(buf + j, buf + i, ctr) < 0);
      i = j;
    }
    // And each individual element should be intact.
    for (uint32_t i = 0, d = 0; i < 256; ++i) {
      ASSERT(buf[i - d] + d == buf[i]);
      if (++d == esz) d = 0;
    }
    // Some silly single-byte stuff to put the array back in order, and also
    // test comparator returning zero.
    uint8_t mask = 0xf0;
    fn(buf, nelem * esz, 1, masked_cmp, &mask);
    for (uint32_t i = 0; i < 256; ++i) {
      ASSERT((buf[i] ^ i) <= 15);
    }
    mask = 0x0f;
    for (uint32_t i = 0; i < 256; i += 16) {
      fn(buf + i, 16, 1, masked_cmp, &mask);
    }
    for (uint32_t i = 0; i < 256; ++i) {
      ASSERT(buf[i] == i);
    }
  }
  ASSERT(ctr[1] >= 3);
}

int main(int argc, const char** argv) {
  ASSERT(argc >= 2);
  void* lib = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
  if (!lib) FATAL("Could not dlopen %s", argv[1]);
  test_qsort_r(lib);
  
  FILE* f = argv[2] ? fopen(argv[2], "w") : stdout;
  fputs("OK\n", f);
  fclose(f);
  return 0;
}
