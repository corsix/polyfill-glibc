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

static void test_totalorder_f32ptr(void* lib) {
  int (*fn)(float*, float*);
  fn = dlvsym(lib, "totalorder_f32ptr", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "totalorderf", "GLIBC_2.31");
    if (!fn) {
      FATAL("Could not find totalorder_f32ptr");
    }
  }

  u__float32 prev;
  u__float32 curr;
  prev.u = 0;
  curr.u = 1;
  for (int i = 0; i < 31; ++i) {
    ASSERT(fn(&prev.f, &curr.f) != 0);
    curr.u ^= 0x80000000;
    ASSERT(fn(&prev.f, &curr.f) == 0);
    prev.u ^= 0x80000000;
    ASSERT(fn(&prev.f, &curr.f) == 0);
    curr.u ^= 0x80000000;
    ASSERT(fn(&prev.f, &curr.f) != 0);
    prev.u = curr.u;
    ASSERT(fn(&prev.f, &curr.f) != 0);
    curr.u = (curr.u << 1) | 1;
  }
}

static void test_totalorder_f64ptr(void* lib) {
  int (*fn)(double*, double*);
  fn = dlvsym(lib, "totalorder_f64ptr", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "totalorder", "GLIBC_2.31");
    if (!fn) {
      FATAL("Could not find totalorder_f64ptr");
    }
  }

  u__float64 prev;
  u__float64 curr;
  prev.u = 0;
  curr.u = 1;
  for (int i = 0; i < 63; ++i) {
    ASSERT(fn(&prev.f, &curr.f) != 0);
    curr.u ^= (1ull << 63);
    ASSERT(fn(&prev.f, &curr.f) == 0);
    prev.u ^= (1ull << 63);
    ASSERT(fn(&prev.f, &curr.f) == 0);
    curr.u ^= (1ull << 63);
    ASSERT(fn(&prev.f, &curr.f) != 0);
    prev.u = curr.u;
    ASSERT(fn(&prev.f, &curr.f) != 0);
    curr.u = (curr.u << 1) | 1u;
  }
}

static void test_totalorder_f80ptr(void* lib) {
  int (*fn)(long double*, long double*);
  fn = dlvsym(lib, "totalorder_f80ptr", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "totalorderl", "GLIBC_2.31");
    if (!fn) {
      FATAL("Could not find totalorder_f80ptr");
    }
  }

  u__float80 prev;
  u__float80 curr;
  prev.u64[0] = 0;
  prev.u64[1] = 0;
  curr.u64[0] = 1;
  curr.u64[1] = 0;
  for (int i = 0; i < 79; ++i) {
    ASSERT(fn(&prev.f, &curr.f) != 0);
    curr.u64[1] ^= (1ull << 15);
    ASSERT(fn(&prev.f, &curr.f) == 0);
    prev.u64[1] ^= (1ull << 15);
    ASSERT(fn(&prev.f, &curr.f) == 0);
    curr.u64[1] ^= (1ull << 15);
    ASSERT(fn(&prev.f, &curr.f) != 0);
    prev.u64[0] = curr.u64[0];
    prev.u64[1] = curr.u64[1];
    ASSERT(fn(&prev.f, &curr.f) != 0);
    if (curr.u64[0] == ~0ull) {
      curr.u64[1] = (curr.u64[1] << 1) | 1u;
    } else {
      curr.u64[0] = (curr.u64[0] << 1) | 1u;
    }
  }
}

static void test_totalorder_f128ptr(void* lib) {
  int (*fn)(__float128*, __float128*);
  fn = dlvsym(lib, "totalorder_f128ptr", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "totalorderf128", "GLIBC_2.31");
    if (!fn) {
      FATAL("Could not find totalorder_f128ptr");
    }
  }

  u__float128 prev;
  u__float128 curr;
  prev.u64[0] = 0;
  prev.u64[1] = 0;
  curr.u64[0] = 1;
  curr.u64[1] = 0;
  for (int i = 0; i < 127; ++i) {
    ASSERT(fn(&prev.f, &curr.f) != 0);
    curr.u64[1] ^= (1ull << 63);
    ASSERT(fn(&prev.f, &curr.f) == 0);
    prev.u64[1] ^= (1ull << 63);
    ASSERT(fn(&prev.f, &curr.f) == 0);
    curr.u64[1] ^= (1ull << 63);
    ASSERT(fn(&prev.f, &curr.f) != 0);
    prev.u64[0] = curr.u64[0];
    prev.u64[1] = curr.u64[1];
    ASSERT(fn(&prev.f, &curr.f) != 0);
    if (curr.u64[0] == ~0ull) {
      curr.u64[1] = (curr.u64[1] << 1) | 1u;
    } else {
      curr.u64[0] = (curr.u64[0] << 1) | 1u;
    }
  }
}

static void test_totalordermag_f32ptr(void* lib) {
  int (*fn)(float*, float*);
  fn = dlvsym(lib, "totalordermag_f32ptr", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "totalordermagf", "GLIBC_2.31");
    if (!fn) {
      FATAL("Could not find totalordermag_f32ptr");
    }
  }

  u__float32 prev;
  u__float32 curr;
  prev.u = 0;
  curr.u = 1;
  for (int i = 0; i < 31; ++i) {
    ASSERT(fn(&curr.f, &prev.f) == 0);
    prev.u = curr.u;
    curr.u ^= 0x80000000;
    ASSERT(fn(&curr.f, &prev.f) != 0);
    curr.u = (curr.u << 1) | 1;
  }
}

static void test_totalordermag_f64ptr(void* lib) {
  int (*fn)(double*, double*);
  fn = dlvsym(lib, "totalordermag_f64ptr", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "totalordermag", "GLIBC_2.31");
    if (!fn) {
      FATAL("Could not find totalordermag_f64ptr");
    }
  }

  u__float64 prev;
  u__float64 curr;
  prev.u = 0;
  curr.u = 1;
  for (int i = 0; i < 63; ++i) {
    ASSERT(fn(&curr.f, &prev.f) == 0);
    prev.u = curr.u;
    curr.u ^= 1ull << 63;
    ASSERT(fn(&curr.f, &prev.f) != 0);
    curr.u = (curr.u << 1) | 1;
  }
}

static void test_totalordermag_f80ptr(void* lib) {
  int (*fn)(long double*, long double*);
  fn = dlvsym(lib, "totalordermag_f80ptr", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "totalordermagl", "GLIBC_2.31");
    if (!fn) {
      FATAL("Could not find totalordermag_f80ptr");
    }
  }

  u__float80 prev;
  u__float80 curr;
  prev.u64[0] = 0;
  prev.u64[1] = 0;
  curr.u64[0] = 1;
  curr.u64[1] = 0;
  for (int i = 0; i < 79; ++i) {
    ASSERT(fn(&curr.f, &prev.f) == 0);
    prev.u64[0] = curr.u64[0];
    prev.u64[1] = curr.u64[1];
    curr.u64[1] ^= 1ull << 15;
    ASSERT(fn(&curr.f, &prev.f) != 0);
    if (curr.u64[0] == ~0ull) {
      curr.u64[1] = (curr.u64[1] << 1) | 1u;
    } else {
      curr.u64[0] = (curr.u64[0] << 1) | 1u;
    }
  }
}

static void test_totalordermag_f128ptr(void* lib) {
  int (*fn)(__float128*, __float128*);
  fn = dlvsym(lib, "totalordermag_f128ptr", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "totalordermagf128", "GLIBC_2.31");
    if (!fn) {
      FATAL("Could not find totalordermag_f128ptr");
    }
  }

  u__float128 prev;
  u__float128 curr;
  prev.u64[0] = 0;
  prev.u64[1] = 0;
  curr.u64[0] = 1;
  curr.u64[1] = 0;
  for (int i = 0; i < 127; ++i) {
    ASSERT(fn(&curr.f, &prev.f) == 0);
    prev.u64[0] = curr.u64[0];
    prev.u64[1] = curr.u64[1];
    curr.u64[1] ^= 1ull << 63;
    ASSERT(fn(&curr.f, &prev.f) != 0);
    if (curr.u64[0] == ~0ull) {
      curr.u64[1] = (curr.u64[1] << 1) | 1u;
    } else {
      curr.u64[0] = (curr.u64[0] << 1) | 1u;
    }
  }
}

int main(int argc, const char** argv) {
  ASSERT(argc >= 2);
  void* lib = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
  if (!lib) FATAL("Could not dlopen %s", argv[1]);
  test_totalorder_f32ptr(lib);
  test_totalorder_f64ptr(lib);
  test_totalorder_f80ptr(lib);
  test_totalorder_f128ptr(lib);
  test_totalordermag_f32ptr(lib);
  test_totalordermag_f64ptr(lib);
  test_totalordermag_f80ptr(lib);
  test_totalordermag_f128ptr(lib);

  FILE* f = argv[2] ? fopen(argv[2], "w") : stdout;
  fputs("OK\n", f);
  fclose(f);
  return 0;
}
