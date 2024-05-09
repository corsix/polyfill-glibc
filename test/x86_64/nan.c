#include <dlfcn.h>
#include "../../src/common.h"

typedef union {
  float f;
  uint32_t u;
} u__float32;

typedef union {
  double f;
  uint64_t u;
} u__float64;

typedef union {
  long double f;
  uint16_t u16[8];
  uint64_t u64[2];
} u__float80;

typedef union {
  __float128 f;
  uint16_t u16[8];
  uint64_t u64[2];
} u__float128;

static void test_issignalingf(void* lib) {
  int (*fn)(float);
  fn = dlvsym(lib, "__issignalingf", "POLYFILL");
  if (!fn) {
    fn = dlsym(lib, "__issignalingf");
    if (!fn) {
      FATAL("Could not find __issignalingf");
    }
  }

  u__float32 x;
  x.u = 0;
  for (uint32_t s = 0; s <= 1; ++s) {
    for (int16_t exp = -2; exp <= 2; ++exp) {
      for (uint32_t m22 = 0; m22 <= 1; ++m22) {
        x.u = (s << 31) | ((exp & 0xffu) << 23) | (m22 << 22);
        ASSERT(fn(x.f) == 0);
        for (uint32_t m = 0; m < 22; ++m) {
          x.u ^= (1u << m);
          ASSERT(fn(x.f) == ((m22 == 0) && (exp == -1)));
          x.u ^= (1u << m);
        }
      }
    }
  }
}

static void test_issignaling(void* lib) {
  int (*fn)(double);
  fn = dlvsym(lib, "__issignaling", "POLYFILL");
  if (!fn) {
    fn = dlsym(lib, "__issignaling");
    if (!fn) {
      FATAL("Could not find __issignaling");
    }
  }

  u__float64 x;
  x.u = 0;
  for (uint64_t s = 0; s <= 1; ++s) {
    for (int16_t exp = -2; exp <= 2; ++exp) {
      for (uint64_t m51 = 0; m51 <= 1; ++m51) {
        x.u = (s << 63) | ((exp & 0x7ffull) << 52) | (m51 << 51);
        ASSERT(fn(x.f) == 0);
        for (uint32_t m = 0; m < 51; ++m) {
          x.u ^= (1ull << m);
          ASSERT(fn(x.f) == ((m51 == 0) && (exp == -1)));
          x.u ^= (1ull << m);
        }
      }
    }
  }
}

static void test_issignalingl(void* lib) {
  int (*fn)(long double);
  fn = dlvsym(lib, "__issignalingl", "POLYFILL");
  if (!fn) {
    fn = dlsym(lib, "__issignalingl");
    if (!fn) {
      FATAL("Could not find __issignalingl");
    }
  }

  u__float80 x;
  x.u64[0] = 0;
  x.u64[1] = 0;
  for (uint32_t s = 0; s <= 1; ++s) {
    for (int16_t exp = -2; exp <= 2; ++exp) {
      x.u16[4] = (s << 15) | (exp & 0x7fff);
      for (uint64_t m62 = 0; m62 <= 3; ++m62) {
        x.u64[0] = (m62 << 62);
        ASSERT(fn(x.f) == (exp != 0 && m62 < 2));
        for (uint32_t m = 0; m < 62; ++m) {
          x.u64[0] ^= (1ull << m);
          ASSERT(fn(x.f) == ((exp != 0 && m62 < 2) || (exp == -1 && !(m62 & 1))));
          x.u64[0] ^= (1ull << m);
        }
      }
    }
  }
}

static void test_isnanf128(void* lib) {
  int (*fn)(__float128);
  fn = dlvsym(lib, "__isnanf128", "POLYFILL");
  if (!fn) {
    fn = dlsym(lib, "__isnanf128");
    if (!fn) {
      FATAL("Could not find __isnanf128");
    }
  }

  u__float128 x;
  x.u64[0] = 0;
  x.u64[1] = 0;
  for (uint32_t s = 0; s <= 1; ++s) {
    for (int16_t exp = -2; exp <= 2; ++exp) {
      x.u16[7] = (s << 15) | (exp & 0x7fff);
      ASSERT(fn(x.f) == 0);
      for (uint32_t m = 0; m < 112; ++m) {
        x.u16[m >> 4] = (1u << (m & 15));
        ASSERT(fn(x.f) == (exp == -1));
        x.u16[m >> 4] = 0;
      }
    }
  }
}

int main(int argc, const char** argv) {
  ASSERT(argc >= 2);
  void* lib = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
  if (!lib) FATAL("Could not dlopen %s", argv[1]);
  test_isnanf128(lib);
  test_issignalingf(lib);
  test_issignaling(lib);
  test_issignalingl(lib);

  FILE* f = argv[2] ? fopen(argv[2], "w") : stdout;
  fputs("OK\n", f);
  fclose(f);
  return 0;
}
