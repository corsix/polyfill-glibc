#include <dlfcn.h>
#include "../../src/common.h"

static void test_bit_ceil_uc(void* lib) {
  unsigned int (*fn)(unsigned char);
  fn = dlvsym(lib, "stdc_bit_ceil_uc", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_bit_ceil_uc", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_bit_ceil_uc");
    }
  }
  unsigned int res = 1;
  for (unsigned int i = 0; i <= 255; ++i) {
    if (i > res) res <<= 1;
    // The spec leaves the overflow behaviour undefined; glibc on x86_64
    // returns a u32 with the true result, whereas glibc on aarch64 returns
    // zero.
#ifdef TEST_AARCH64
    ASSERT(fn(i) == (res & 0xff));
#else
    ASSERT(fn(i) == res);
#endif
  }
}

static void test_bit_ceil_us(void* lib) {
  unsigned int (*fn)(unsigned short);
  fn = dlvsym(lib, "stdc_bit_ceil_us", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_bit_ceil_us", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_bit_ceil_us");
    }
  }
  unsigned int res = 1;
  for (unsigned int i = 0; i < 8; ++i) {
    if (i > res) res <<= 1;
    ASSERT(fn(i) == res);
  }
  for (unsigned int e = 3; e <= 16; ++e) {
    for (int j = -2; j <= 2; ++j) {
      unsigned int i = (1u << e) + j;
      if (i != (unsigned short)i) continue;
      if (i > res) res <<= 1;
      // The spec leaves the overflow behaviour undefined; glibc on x86_64
      // returns a u32 with the true result, whereas glibc on aarch64 returns
      // zero.
#ifdef TEST_AARCH64
      ASSERT(fn(i) == (res & 0xffff));
#else
      ASSERT(fn(i) == res);
#endif
    }
  }
}

static void test_bit_ceil_ui(void* lib) {
  unsigned int (*fn)(unsigned int);
  fn = dlvsym(lib, "stdc_bit_ceil_ui", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_bit_ceil_ui", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_bit_ceil_us");
    }
  }
  unsigned int res = 1;
  for (unsigned int i = 0; i < 8; ++i) {
    if (i > res) res <<= 1;
    ASSERT(fn(i) == res);
  }
  for (unsigned int e = 3; e <= 31; ++e) {
    for (int j = -2; j <= 2; ++j) {
      unsigned int i = (1u << e) + j;
      if (i > res) res <<= 1;
      // The spec leaves the overflow behaviour undefined; glibc returns zero.
      ASSERT(fn(i) == res);
    }
  }
}

static void test_bit_ceil_ul(void* lib) {
  unsigned long (*fn)(unsigned long);
  fn = dlvsym(lib, "stdc_bit_ceil_ul", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_bit_ceil_ul", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_bit_ceil_ul");
    }
  }
  unsigned long res = 1;
  for (unsigned int i = 0; i < 8; ++i) {
    if (i > res) res <<= 1;
    ASSERT(fn(i) == res);
  }
  for (unsigned int e = 3; e <= 31; ++e) {
    for (int j = -2; j <= 2; ++j) {
      unsigned long i = (1ull << e) + j;
      if (i > res) res <<= 1;
      // The spec leaves the overflow behaviour undefined; glibc returns zero.
      ASSERT(fn(i) == res);
    }
  }
}

static void test_bit_floor_uc(void* lib) {
  unsigned char (*fn)(unsigned char);
  fn = dlvsym(lib, "stdc_bit_floor_uc", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_bit_floor_uc", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_bit_floor_uc");
    }
  }
  unsigned int res = 128;
  for (int i = 255; i >= 0; --i) {
    ASSERT(fn(i) == res);
    if ((unsigned int)i == res) res >>= 1;
  }
}

static void test_bit_floor_us(void* lib) {
  unsigned short (*fn)(unsigned short);
  fn = dlvsym(lib, "stdc_bit_floor_us", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_bit_floor_us", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_bit_floor_us");
    }
  }
  unsigned int res = 1u << 15;
  for (unsigned int e = 16; e >= 3; --e) {
    for (int j = 2; j >= -2; --j) {
      unsigned int i = (1u << e) + j;
      if (i != (unsigned short)i) continue;
      ASSERT(fn(i) == res);
      if (i == res) res >>= 1;
    }
  }
  for (int i = 7; i >= 0; --i) {
    ASSERT(fn(i) == res);
    if ((unsigned int)i == res) res >>= 1;
  }
}

static void test_bit_floor_ui(void* lib) {
  unsigned int (*fn)(unsigned int);
  fn = dlvsym(lib, "stdc_bit_floor_ui", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_bit_floor_ui", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_bit_floor_ui");
    }
  }
  unsigned int res = 1u << 31;
  for (unsigned int e = 31; e >= 3; --e) {
    for (int j = 2; j >= -2; --j) {
      unsigned int i = (1u << e) + j;
      ASSERT(fn(i) == res);
      if (i == res) res >>= 1;
    }
  }
  for (int i = 7; i >= 0; --i) {
    ASSERT(fn(i) == res);
    if ((unsigned int)i == res) res >>= 1;
  }
}

static void test_bit_floor_ul(void* lib) {
  unsigned long (*fn)(unsigned long);
  fn = dlvsym(lib, "stdc_bit_floor_ul", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_bit_floor_ul", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_bit_floor_ul");
    }
  }
  unsigned long res = 1ull << 63;
  for (unsigned int e = 63; e >= 3; --e) {
    for (int j = 2; j >= -2; --j) {
      unsigned long i = (1ull << e) + j;
      ASSERT(fn(i) == res);
      if (i == res) res >>= 1;
    }
  }
  for (int i = 7; i >= 0; --i) {
    ASSERT(fn(i) == res);
    if ((unsigned int)i == res) res >>= 1;
  }
}

static void test_bit_width_uc(void* lib) {
  int (*fn)(unsigned char);
  fn = dlvsym(lib, "stdc_bit_width_uc", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_bit_width_uc", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_bit_width_uc");
    }
  }
  int res = 8;
  for (int i = 255; i >= 0; --i) {
    ASSERT(fn(i) == res);
    if (!(i & (i - 1))) res -= 1;
  }
}

static void test_bit_width_us(void* lib) {
  int (*fn)(unsigned short);
  fn = dlvsym(lib, "stdc_bit_width_us", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_bit_width_us", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_bit_width_us");
    }
  }
  int res = 16;
  for (unsigned int e = 16; e >= 3; --e) {
    for (int j = 2; j >= -2; --j) {
      unsigned int i = (1u << e) + j;
      if (i != (unsigned short)i) continue;
      ASSERT(fn(i) == res);
      if (!(i & (i - 1))) res -= 1;
    }
  }
  for (int i = 7; i >= 0; --i) {
    ASSERT(fn(i) == res);
    if (!(i & (i - 1))) res -= 1;
  }
}

static void test_bit_width_ui(void* lib) {
  int (*fn)(unsigned int);
  fn = dlvsym(lib, "stdc_bit_width_ui", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_bit_width_ui", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_bit_width_ui");
    }
  }
  int res = 32;
  for (unsigned int e = 31; e >= 3; --e) {
    for (int j = 2; j >= -2; --j) {
      unsigned int i = (1u << e) + j;
      ASSERT(fn(i) == res);
      if (!(i & (i - 1))) res -= 1;
    }
  }
  for (int i = 7; i >= 0; --i) {
    ASSERT(fn(i) == res);
    if (!(i & (i - 1))) res -= 1;
  }
}

static void test_bit_width_ul(void* lib) {
  int (*fn)(unsigned long);
  fn = dlvsym(lib, "stdc_bit_width_ul", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_bit_width_ul", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_bit_width_ul");
    }
  }
  int res = 64;
  for (unsigned int e = 63; e >= 3; --e) {
    for (int j = 2; j >= -2; --j) {
      unsigned long i = (1ull << e) + j;
      ASSERT(fn(i) == res);
      if (!(i & (i - 1))) res -= 1;
    }
  }
  for (int i = 7; i >= 0; --i) {
    ASSERT(fn(i) == res);
    if (!(i & (i - 1))) res -= 1;
  }
}

static int popcnt_ref(unsigned long x) {
  int res = 0;
  while (x) {
    x &= x - 1;
    ++res;
  }
  return res;
}

static void test_popcnt(void* lib, const char* name, int width) {
  int (*fn)(unsigned long);
  fn = dlvsym(lib, name, "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, name, "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find %s", name);
    }
  }
  for (int a = 0; a < width; ++a) {
    for (int b = 0; b <= a; ++b) {
      ASSERT(fn((1ull << a) - (1ull << b)) == a - b);
    }
  }
  for (int b = 0; b < width; ++b) {
    ASSERT(fn(0 - (1ull << b)) == width - b);
  }
  unsigned long mask = ((1ull << (width - 1)) << 1) - 1;
  unsigned long x = 1;
  for (int i = 0; i < 20; ++i) {
    x *= 0x21f0aaad;
    x ^= (x >> 15);
    ASSERT(fn(x & mask) == popcnt_ref(x & mask));
  }
}

static void test_invpopcnt(void* lib, const char* name, int width) {
  int (*fn)(unsigned long);
  fn = dlvsym(lib, name, "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, name, "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find %s", name);
    }
  }
  for (int a = 0; a < width; ++a) {
    for (int b = 0; b <= a; ++b) {
      ASSERT(fn(~((1ull << a) - (1ull << b))) == a - b);
    }
  }
  for (int b = 0; b < width; ++b) {
    ASSERT(fn(~(0 - (1ull << b))) == width - b);
  }
  unsigned long mask = ((1ull << (width - 1)) << 1) - 1;
  unsigned long x = 1;
  for (int i = 0; i < 20; ++i) {
    x *= 0x21f0aaad;
    x ^= (x >> 15);
    ASSERT(fn((~x) & mask) == popcnt_ref(x & mask));
  }
}

static bool force_slow_popcnt(void* lib) {
#ifdef TEST_AARCH64
  (void)lib;
#else
  uint8_t* impl = dlvsym(lib, "stdc_count_ones_ul", "POLYFILL");
  if (impl) {
    // Search for a movzx instruction with a rip-rel operand.
    while (impl[0] != 0x0F || impl[1] != 0xB6 || (impl[2] & 0xC7) != 0x05) ++impl;
    // Determine the rip-rel target.
    impl += 7;
    impl += ((uint32_t*)impl)[-1];
    // Un-set the high bit.
    if (*impl & 0x80) {
      *impl = 0x7f;
      return true;
    }
  }
#endif
  return false;
}

static void test_first_leading_one_uc(void* lib) {
  int (*fn)(unsigned char);
  fn = dlvsym(lib, "stdc_first_leading_one_uc", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_leading_one_uc", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_leading_one_uc");
    }
  }
  ASSERT(fn(0) == 0);
  int res = 9;
  for (int i = 1; i <= 255; ++i) {
    if (!(i & (i - 1))) res -= 1;
    ASSERT(fn(i) == res);
  }
}

static void test_first_leading_one_us(void* lib) {
  int (*fn)(unsigned short);
  fn = dlvsym(lib, "stdc_first_leading_one_us", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_leading_one_us", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_leading_one_us");
    }
  }
  ASSERT(fn(0) == 0);
  int res = 17;
  for (int i = 1; i <= 7; ++i) {
    if (!(i & (i - 1))) res -= 1;
    ASSERT(fn(i) == res);
  }
  for (unsigned int e = 3; e <= 16; ++e) {
    for (int j = -2; j <= 2; ++j) {
      unsigned int i = (1u << e) + j;
      if (i != (unsigned short)i) continue;
      if (!(i & (i - 1))) res -= 1;
      ASSERT(fn(i) == res);
    }
  }
}

static void test_first_leading_one_ui(void* lib) {
  int (*fn)(unsigned int);
  fn = dlvsym(lib, "stdc_first_leading_one_ui", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_leading_one_ui", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_leading_one_ui");
    }
  }
  ASSERT(fn(0) == 0);
  int res = 33;
  for (int i = 1; i <= 7; ++i) {
    if (!(i & (i - 1))) res -= 1;
    ASSERT(fn(i) == res);
  }
  for (unsigned int e = 3; e <= 31; ++e) {
    for (int j = -2; j <= 2; ++j) {
      unsigned int i = (1u << e) + j;
      if (!(i & (i - 1))) res -= 1;
      ASSERT(fn(i) == res);
    }
  }
}

static void test_first_leading_one_ul(void* lib) {
  int (*fn)(unsigned long);
  fn = dlvsym(lib, "stdc_first_leading_one_ul", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_leading_one_ul", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_leading_one_ul");
    }
  }
  ASSERT(fn(0) == 0);
  int res = 65;
  for (int i = 1; i <= 7; ++i) {
    if (!(i & (i - 1))) res -= 1;
    ASSERT(fn(i) == res);
  }
  for (unsigned int e = 3; e <= 63; ++e) {
    for (int j = -2; j <= 2; ++j) {
      unsigned long i = (1ull << e) + j;
      if (!(i & (i - 1))) res -= 1;
      ASSERT(fn(i) == res);
    }
  }
}

static void test_first_leading_zero_uc(void* lib) {
  int (*fn)(unsigned char);
  fn = dlvsym(lib, "stdc_first_leading_zero_uc", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_leading_zero_uc", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_leading_zero_uc");
    }
  }
  ASSERT(fn(~0) == 0);
  int res = 9;
  for (int i = 1; i <= 255; ++i) {
    if (!(i & (i - 1))) res -= 1;
    ASSERT(fn(~i) == res);
  }
}

static void test_first_leading_zero_us(void* lib) {
  int (*fn)(unsigned short);
  fn = dlvsym(lib, "stdc_first_leading_zero_us", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_leading_zero_us", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_leading_zero_us");
    }
  }
  ASSERT(fn(~0) == 0);
  int res = 17;
  for (int i = 1; i <= 7; ++i) {
    if (!(i & (i - 1))) res -= 1;
    ASSERT(fn(~i) == res);
  }
  for (unsigned int e = 3; e <= 16; ++e) {
    for (int j = -2; j <= 2; ++j) {
      unsigned int i = (1u << e) + j;
      if (i != (unsigned short)i) continue;
      if (!(i & (i - 1))) res -= 1;
      ASSERT(fn(~i) == res);
    }
  }
}

static void test_first_leading_zero_ui(void* lib) {
  int (*fn)(unsigned int);
  fn = dlvsym(lib, "stdc_first_leading_zero_ui", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_leading_zero_ui", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_leading_zero_ui");
    }
  }
  ASSERT(fn(~0) == 0);
  int res = 33;
  for (int i = 1; i <= 7; ++i) {
    if (!(i & (i - 1))) res -= 1;
    ASSERT(fn(~i) == res);
  }
  for (unsigned int e = 3; e <= 31; ++e) {
    for (int j = -2; j <= 2; ++j) {
      unsigned int i = (1u << e) + j;
      if (!(i & (i - 1))) res -= 1;
      ASSERT(fn(~i) == res);
    }
  }
}

static void test_first_leading_zero_ul(void* lib) {
  int (*fn)(unsigned long);
  fn = dlvsym(lib, "stdc_first_leading_zero_ul", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_leading_zero_ul", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_leading_zero_ul");
    }
  }
  ASSERT(fn(~0ull) == 0);
  int res = 65;
  for (int i = 1; i <= 7; ++i) {
    if (!(i & (i - 1))) res -= 1;
    ASSERT(fn(~i) == res);
  }
  for (unsigned int e = 3; e <= 63; ++e) {
    for (int j = -2; j <= 2; ++j) {
      unsigned long i = (1ull << e) + j;
      if (!(i & (i - 1))) res -= 1;
      ASSERT(fn(~i) == res);
    }
  }
}

static void test_first_trailing_one_uc(void* lib) {
  int (*fn)(unsigned char);
  fn = dlvsym(lib, "stdc_first_trailing_one_uc", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_trailing_one_uc", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_trailing_one_uc");
    }
  }
  ASSERT(fn(0) == 0);
  for (int a = 0; a <= 7; ++a) {
    ASSERT(fn(1u << a) == a + 1);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn((1u << a) | (1u << b)) == b + 1);
    }
  }
}

static void test_first_trailing_one_us(void* lib) {
  int (*fn)(unsigned short);
  fn = dlvsym(lib, "stdc_first_trailing_one_us", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_trailing_one_us", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_trailing_one_us");
    }
  }
  ASSERT(fn(0) == 0);
  for (int a = 0; a <= 15; ++a) {
    ASSERT(fn(1u << a) == a + 1);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn((1u << a) | (1u << b)) == b + 1);
    }
  }
}

#ifdef TEST_AARCH64
static void test_first_trailing_one_ui(void* lib) {
  int (*fn)(unsigned int);
  fn = dlvsym(lib, "stdc_first_trailing_one_ui", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_trailing_one_ui", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_trailing_one_ui");
    }
  }
  ASSERT(fn(0) == 0);
  for (int a = 0; a <= 31; ++a) {
    ASSERT(fn(1u << a) == a + 1);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn((1u << a) | (1u << b)) == b + 1);
    }
  }
}

static void test_first_trailing_one_ul(void* lib) {
  int (*fn)(unsigned long);
  fn = dlvsym(lib, "stdc_first_trailing_one_ul", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_trailing_one_ul", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_trailing_one_ul");
    }
  }
  ASSERT(fn(0) == 0);
  for (int a = 0; a <= 63; ++a) {
    ASSERT(fn(1ull << a) == a + 1);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn((1ull << a) | (1ull << b)) == b + 1);
    }
  }
}
#endif

static void test_first_trailing_zero_uc(void* lib) {
  int (*fn)(unsigned char);
  fn = dlvsym(lib, "stdc_first_trailing_zero_uc", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_trailing_zero_uc", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_trailing_zero_uc");
    }
  }
  ASSERT(fn(~0) == 0);
  for (int a = 0; a <= 7; ++a) {
    ASSERT(fn(~(1u << a)) == a + 1);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn(~((1u << a) | (1u << b))) == b + 1);
    }
  }
}

static void test_first_trailing_zero_us(void* lib) {
  int (*fn)(unsigned short);
  fn = dlvsym(lib, "stdc_first_trailing_zero_us", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_trailing_zero_us", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_trailing_zero_us");
    }
  }
  ASSERT(fn(~0) == 0);
  for (int a = 0; a <= 15; ++a) {
    ASSERT(fn(~(1u << a)) == a + 1);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn(~((1u << a) | (1u << b))) == b + 1);
    }
  }
}

static void test_first_trailing_zero_ui(void* lib) {
  int (*fn)(unsigned int);
  fn = dlvsym(lib, "stdc_first_trailing_zero_ui", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_trailing_zero_ui", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_trailing_zero_ui");
    }
  }
  ASSERT(fn(~0) == 0);
  for (int a = 0; a <= 31; ++a) {
    ASSERT(fn(~(1u << a)) == a + 1);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn(~((1u << a) | (1u << b))) == b + 1);
    }
  }
}

static void test_first_trailing_zero_ul(void* lib) {
  int (*fn)(unsigned long);
  fn = dlvsym(lib, "stdc_first_trailing_zero_ul", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_first_trailing_zero_ul", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_first_trailing_zero_ul");
    }
  }
  ASSERT(fn(~0ull) == 0);
  for (int a = 0; a <= 63; ++a) {
    ASSERT(fn(~(1ull << a)) == a + 1);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn(~((1ull << a) | (1ull << b))) == b + 1);
    }
  }
}

static void test_has_single_bit_uc(void* lib) {
  bool (*fn)(unsigned char);
  fn = dlvsym(lib, "stdc_has_single_bit_uc", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_has_single_bit_uc", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_has_single_bit_uc");
    }
  }
  for (int i = 0; i <= 255; ++i) {
    ASSERT(fn(i) == ((i != 0) && !(i & (i - 1))));
  }
}

static void test_has_single_bit_us(void* lib) {
  bool (*fn)(unsigned short);
  fn = dlvsym(lib, "stdc_has_single_bit_us", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_has_single_bit_us", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_has_single_bit_us");
    }
  }
  for (unsigned int e = 0; e <= 16; ++e) {
    for (int j = -2; j <= 2; ++j) {
      unsigned short i = (1u << e) + j;
      ASSERT(fn(i) == ((i != 0) && !(i & (i - 1))));
    }
  }
}

static void test_has_single_bit_ui(void* lib) {
  bool (*fn)(unsigned int);
  fn = dlvsym(lib, "stdc_has_single_bit_ui", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_has_single_bit_ui", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_has_single_bit_ui");
    }
  }
  for (unsigned int e = 0; e <= 31; ++e) {
    for (int j = -2; j <= 2; ++j) {
      unsigned int i = (1u << e) + j;
      ASSERT(fn(i) == ((i != 0) && !(i & (i - 1))));
    }
  }
}

static void test_has_single_bit_ul(void* lib) {
  bool (*fn)(unsigned long);
  fn = dlvsym(lib, "stdc_has_single_bit_ul", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_has_single_bit_ul", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_has_single_bit_ul");
    }
  }
  for (unsigned int e = 0; e <= 63; ++e) {
    for (int j = -2; j <= 2; ++j) {
      unsigned long i = (1ull << e) + j;
      ASSERT(fn(i) == ((i != 0) && !(i & (i - 1))));
    }
  }
}

static void test_leading_ones_uc(void* lib) {
  int (*fn)(unsigned char);
  fn = dlvsym(lib, "stdc_leading_ones_uc", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_leading_ones_uc", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_leading_ones_uc");
    }
  }
  int res = 0;
  for (int i = 255; i >= 0; --i) {
    ASSERT(fn(~(unsigned int)i) == res);
    if (!(i & (i - 1))) res += 1;
  }
}

static void test_leading_ones_us(void* lib) {
  int (*fn)(unsigned short);
  fn = dlvsym(lib, "stdc_leading_ones_us", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_leading_ones_us", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_leading_ones_us");
    }
  }
  int res = 0;
  for (unsigned int e = 16; e >= 3; --e) {
    for (int j = 2; j >= -2; --j) {
      unsigned int i = (1u << e) + j;
      if (i != (unsigned short)i) continue;
      ASSERT(fn(~i) == res);
      if (!(i & (i - 1))) res += 1;
    }
  }
  for (int i = 7; i >= 0; --i) {
    ASSERT(fn(~(unsigned int)i) == res);
    if (!(i & (i - 1))) res += 1;
  }
}

static void test_leading_ones_ui(void* lib) {
  int (*fn)(unsigned int);
  fn = dlvsym(lib, "stdc_leading_ones_ui", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_leading_ones_ui", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_leading_ones_ui");
    }
  }
  int res = 0;
  for (unsigned int e = 31; e >= 3; --e) {
    for (int j = 2; j >= -2; --j) {
      unsigned int i = (1u << e) + j;
      ASSERT(fn(~i) == res);
      if (!(i & (i - 1))) res += 1;
    }
  }
  for (int i = 7; i >= 0; --i) {
    ASSERT(fn(~(unsigned int)i) == res);
    if (!(i & (i - 1))) res += 1;
  }
}

static void test_leading_ones_ul(void* lib) {
  int (*fn)(unsigned long);
  fn = dlvsym(lib, "stdc_leading_ones_ul", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_leading_ones_ul", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_leading_ones_ul");
    }
  }
  int res = 0;
  for (unsigned int e = 63; e >= 3; --e) {
    for (int j = 2; j >= -2; --j) {
      unsigned long i = (1ull << e) + j;
      ASSERT(fn(~i) == res);
      if (!(i & (i - 1))) res += 1;
    }
  }
  for (int i = 7; i >= 0; --i) {
    ASSERT(fn(~(unsigned long)i) == res);
    if (!(i & (i - 1))) res += 1;
  }
}

static void test_leading_zeros_uc(void* lib) {
  int (*fn)(unsigned char);
  fn = dlvsym(lib, "stdc_leading_zeros_uc", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_leading_zeros_uc", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_leading_zeros_uc");
    }
  }
  int res = 0;
  for (int i = 255; i >= 0; --i) {
    ASSERT(fn(i) == res);
    if (!(i & (i - 1))) res += 1;
  }
}

static void test_leading_zeros_us(void* lib) {
  int (*fn)(unsigned short);
  fn = dlvsym(lib, "stdc_leading_zeros_us", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_leading_zeros_us", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_leading_zeros_us");
    }
  }
  int res = 0;
  for (unsigned int e = 16; e >= 3; --e) {
    for (int j = 2; j >= -2; --j) {
      unsigned int i = (1u << e) + j;
      if (i != (unsigned short)i) continue;
      ASSERT(fn(i) == res);
      if (!(i & (i - 1))) res += 1;
    }
  }
  for (int i = 7; i >= 0; --i) {
    ASSERT(fn(i) == res);
    if (!(i & (i - 1))) res += 1;
  }
}

static void test_leading_zeros_ui(void* lib) {
  int (*fn)(unsigned int);
  fn = dlvsym(lib, "stdc_leading_zeros_ui", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_leading_zeros_ui", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_leading_zeros_ui");
    }
  }
  int res = 0;
  for (unsigned int e = 31; e >= 3; --e) {
    for (int j = 2; j >= -2; --j) {
      unsigned int i = (1u << e) + j;
      ASSERT(fn(i) == res);
      if (!(i & (i - 1))) res += 1;
    }
  }
  for (int i = 7; i >= 0; --i) {
    ASSERT(fn(i) == res);
    if (!(i & (i - 1))) res += 1;
  }
}

static void test_leading_zeros_ul(void* lib) {
  int (*fn)(unsigned long);
  fn = dlvsym(lib, "stdc_leading_zeros_ul", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_leading_zeros_ul", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_leading_zeros_ul");
    }
  }
  int res = 0;
  for (unsigned int e = 63; e >= 3; --e) {
    for (int j = 2; j >= -2; --j) {
      unsigned long i = (1ull << e) + j;
      ASSERT(fn(i) == res);
      if (!(i & (i - 1))) res += 1;
    }
  }
  for (int i = 7; i >= 0; --i) {
    ASSERT(fn(i) == res);
    if (!(i & (i - 1))) res += 1;
  }
}

static void test_trailing_ones_uc(void* lib) {
  int (*fn)(unsigned char);
  fn = dlvsym(lib, "stdc_trailing_ones_uc", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_trailing_ones_uc", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_trailing_ones_uc");
    }
  }
  ASSERT(fn(~0) == 8);
  for (int a = 0; a <= 7; ++a) {
    ASSERT(fn(~(1u << a)) == a);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn(~((1u << a) | (1u << b))) == b);
    }
  }
}

static void test_trailing_ones_us(void* lib) {
  int (*fn)(unsigned short);
  fn = dlvsym(lib, "stdc_trailing_ones_us", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_trailing_ones_us", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_trailing_ones_us");
    }
  }
  ASSERT(fn(~0) == 16);
  for (int a = 0; a <= 15; ++a) {
    ASSERT(fn(~(1u << a)) == a);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn(~((1u << a) | (1u << b))) == b);
    }
  }
}

static void test_trailing_ones_ui(void* lib) {
  int (*fn)(unsigned int);
  fn = dlvsym(lib, "stdc_trailing_ones_ui", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_trailing_ones_ui", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_trailing_ones_ui");
    }
  }
  ASSERT(fn(~0) == 32);
  for (int a = 0; a <= 31; ++a) {
    ASSERT(fn(~(1u << a)) == a);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn(~((1u << a) | (1u << b))) == b);
    }
  }
}

static void test_trailing_ones_ul(void* lib) {
  int (*fn)(unsigned long);
  fn = dlvsym(lib, "stdc_trailing_ones_ul", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_trailing_ones_ul", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_trailing_ones_ul");
    }
  }
  ASSERT(fn(~0ull) == 64);
  for (int a = 0; a <= 63; ++a) {
    ASSERT(fn(~(1ull << a)) == a);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn(~((1ull << a) | (1ull << b))) == b);
    }
  }
}

static void test_trailing_zeros_uc(void* lib) {
  int (*fn)(unsigned char);
  fn = dlvsym(lib, "stdc_trailing_zeros_uc", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_trailing_zeros_uc", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_trailing_zeros_uc");
    }
  }
  ASSERT(fn(0) == 8);
  for (int a = 0; a <= 7; ++a) {
    ASSERT(fn(1u << a) == a);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn((1u << a) | (1u << b)) == b);
    }
  }
}

static void test_trailing_zeros_us(void* lib) {
  int (*fn)(unsigned short);
  fn = dlvsym(lib, "stdc_trailing_zeros_us", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_trailing_zeros_us", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_trailing_zeros_us");
    }
  }
  ASSERT(fn(0) == 16);
  for (int a = 0; a <= 15; ++a) {
    ASSERT(fn(1u << a) == a);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn((1u << a) | (1u << b)) == b);
    }
  }
}

static void test_trailing_zeros_ui(void* lib) {
  int (*fn)(unsigned int);
  fn = dlvsym(lib, "stdc_trailing_zeros_ui", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_trailing_zeros_ui", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_trailing_zeros_ui");
    }
  }
  ASSERT(fn(0) == 32);
  for (int a = 0; a <= 31; ++a) {
    ASSERT(fn(1u << a) == a);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn((1u << a) | (1u << b)) == b);
    }
  }
}

static void test_trailing_zeros_ul(void* lib) {
  int (*fn)(unsigned long);
  fn = dlvsym(lib, "stdc_trailing_zeros_ul", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "stdc_trailing_zeros_ul", "GLIBC_2.39");
    if (!fn) {
      FATAL("Could not find stdc_trailing_zeros_ul");
    }
  }
  ASSERT(fn(0) == 64);
  for (int a = 0; a <= 63; ++a) {
    ASSERT(fn(1ull << a) == a);
    for (int b = 0; b < a; ++b) {
      ASSERT(fn((1ull << a) | (1ull << b)) == b);
    }
  }
}

int main(int argc, const char** argv) {
  ASSERT(argc >= 2);
  void* lib = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
  if (!lib) FATAL("Could not dlopen %s", argv[1]);
  test_bit_ceil_uc(lib);
  test_bit_ceil_us(lib);
  test_bit_ceil_ui(lib);
  test_bit_ceil_ul(lib);
  test_bit_floor_uc(lib);
  test_bit_floor_us(lib);
  test_bit_floor_ui(lib);
  test_bit_floor_ul(lib);
  test_bit_width_uc(lib);
  test_bit_width_us(lib);
  test_bit_width_ui(lib);
  test_bit_width_ul(lib);
  do {
    test_popcnt(lib, "stdc_count_ones_uc", 8);
    test_popcnt(lib, "stdc_count_ones_us", 16);
    test_popcnt(lib, "stdc_count_ones_ui", 32);
    test_popcnt(lib, "stdc_count_ones_ul", 64);
    test_invpopcnt(lib, "stdc_count_zeros_uc", 8);
    test_invpopcnt(lib, "stdc_count_zeros_us", 16);
    test_invpopcnt(lib, "stdc_count_zeros_ui", 32);
    test_invpopcnt(lib, "stdc_count_zeros_ul", 64);
  } while (force_slow_popcnt(lib));
  test_first_leading_one_uc(lib);
  test_first_leading_one_us(lib);
  test_first_leading_one_ui(lib);
  test_first_leading_one_ul(lib);
  test_first_leading_zero_uc(lib);
  test_first_leading_zero_us(lib);
  test_first_leading_zero_ui(lib);
  test_first_leading_zero_ul(lib);
  test_first_trailing_one_uc(lib);
  test_first_trailing_one_us(lib);
#ifdef TEST_AARCH64
  test_first_trailing_one_ui(lib);
  test_first_trailing_one_ul(lib);
#endif
  test_first_trailing_zero_uc(lib);
  test_first_trailing_zero_us(lib);
  test_first_trailing_zero_ui(lib);
  test_first_trailing_zero_ul(lib);
  test_has_single_bit_uc(lib);
  test_has_single_bit_us(lib);
  test_has_single_bit_ui(lib);
  test_has_single_bit_ul(lib);
  test_leading_ones_uc(lib);
  test_leading_ones_us(lib);
  test_leading_ones_ui(lib);
  test_leading_ones_ul(lib);
  test_leading_zeros_uc(lib);
  test_leading_zeros_us(lib);
  test_leading_zeros_ui(lib);
  test_leading_zeros_ul(lib);
  test_trailing_ones_uc(lib);
  test_trailing_ones_us(lib);
  test_trailing_ones_ui(lib);
  test_trailing_ones_ul(lib);
  test_trailing_zeros_uc(lib);
  test_trailing_zeros_us(lib);
  test_trailing_zeros_ui(lib);
  test_trailing_zeros_ul(lib);
  FILE* f = argv[2] ? fopen(argv[2], "w") : stdout;
  fputs("OK\n", f);
  fclose(f);
  return 0;
}
