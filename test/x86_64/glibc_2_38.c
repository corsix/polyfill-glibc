#include <dlfcn.h>
#include <errno.h>
#include <locale.h>
#include <string.h>
#include "../../src/common.h"

#ifndef TEST_AARCH64
static void test_strtoll(void* lib) {
  int* err = NULL;
  long (*fn)(const char*, char**, int);
  fn = dlvsym(lib, "__isoc23_strtol", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "__isoc23_strtoll", "GLIBC_2.38");
    if (!fn) {
      FATAL("Could not find __isoc23_strtoll");
    }
    err = dlsym(lib, "errno");
  }
  if (!err) err = &errno;
  // Some simple cases.
  *err = 0;
  for (int base = 0; base <= 10; base += 10) {
    const char* in = "987";
    char* endp = NULL;
    ASSERT(fn(in, &endp, base) == 987 && *err == 0 && endp == in + 3);
    in = "  +13x";
    ASSERT(fn(in, &endp, base) == 13 && *err == 0 && endp == in + 5);
    in = "  -5b";
    ASSERT(fn(in, &endp, base) == -5 && *err == 0 && endp == in + 4);
    in = " 0b2";
    ASSERT(fn(in, &endp, base) == 0 && *err == 0 && endp == in + 2);
    if (base == 0) {
      in = " 0xfe";
      ASSERT(fn(in, &endp, base) == 254 && *err == 0 && endp == in + 5);
    }
  }
  // Binary parsing
  for (int base = 0; base <= 2; base += 2) {
    const char* in = "0b101";
    char* endp = NULL;
    // Smoke test
    ASSERT(fn(in, &endp, base) == 5 && *err == 0 && endp == in + 5);
    // 64 leading zeros
    in = "0b00000000000000000000000000000000000000000000000000000000000000001";
    ASSERT(fn(in, &endp, base) == 1 && *err == 0 && endp == in + 67);
    // leading space, INT_MAX
    in = " 0b0111111111111111111111111111111111111111111111111111111111111111";
    ASSERT(fn(in, &endp, base) == 0x7fffffffffffffffll && *err == 0 && endp == in + 67);
    // leading +, INT_MAX
    in = "+0b0111111111111111111111111111111111111111111111111111111111111111";
    ASSERT(fn(in, &endp, base) == 0x7fffffffffffffffll && *err == 0 && endp == in + 67);
    // leading -, -INT_MAX
    in = "-0b0111111111111111111111111111111111111111111111111111111111111111";
    ASSERT(fn(in, &endp, base) == -0x7fffffffffffffffll && *err == 0 && endp == in + 67);
    // leading -, zero
    in = "-0b00";
    ASSERT(fn(in, &endp, base) == 0 && *err == 0 && endp == in + 5);
    // leading -, INT_MIN
    in = "-0b1000000000000000000000000000000000000000000000000000000000000000";
    ASSERT(fn(in, &endp, base) == -0x7fffffffffffffffll-1 && *err == 0 && endp == in + 67);
    // leading -, INT_MIN - 1
    in = "-0b1000000000000000000000000000000000000000000000000000000000000001";
    ASSERT(fn(in, &endp, base) == -0x7fffffffffffffffll-1 && *err == ERANGE && endp == in + 67);
    *err = 0;
    // leading +, -INT_MIN
    in = "+0b1000000000000000000000000000000000000000000000000000000000000000";
    ASSERT(fn(in, &endp, base) == 0x7fffffffffffffffll && *err == ERANGE && endp == in + 67);
    *err = 0;
    // > 2**64
    in = "0b100000000000000000000000000000000000000000000000000000000000000000";
    ASSERT(fn(in, &endp, base) == 0x7fffffffffffffffll && *err == ERANGE && endp == in + 68);
    *err = 0;
  }
}

static void test_strtoull(void* lib) {
  int* err = NULL;
  unsigned long (*fn)(const char*, char**, int);
  fn = dlvsym(lib, "__isoc23_strtoul", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "__isoc23_strtoull", "GLIBC_2.38");
    if (!fn) {
      FATAL("Could not find __isoc23_strtoull");
    }
    err = dlsym(lib, "errno");
  }
  if (!err) err = &errno;
  // Some simple cases.
  *err = 0;
  for (int base = 0; base <= 10; base += 10) {
    const char* in = "987";
    char* endp = NULL;
    ASSERT(fn(in, &endp, base) == 987 && *err == 0 && endp == in + 3);
    in = "  +13x";
    ASSERT(fn(in, &endp, base) == 13 && *err == 0 && endp == in + 5);
    in = "  -5b";
    ASSERT(fn(in, &endp, base) == (unsigned long)-5ll && *err == 0 && endp == in + 4);
    in = " 0b2";
    ASSERT(fn(in, &endp, base) == 0 && *err == 0 && endp == in + 2);
    if (base == 0) {
      in = " 0xfe";
      ASSERT(fn(in, &endp, base) == 254 && *err == 0 && endp == in + 5);
    }
  }
  // Binary parsing
  for (int base = 0; base <= 2; base += 2) {
    const char* in = "0b101";
    char* endp = NULL;
    // Smoke test
    ASSERT(fn(in, &endp, base) == 5 && *err == 0 && endp == in + 5);
    // 64 leading zeros
    in = "0b00000000000000000000000000000000000000000000000000000000000000001";
    ASSERT(fn(in, &endp, base) == 1 && *err == 0 && endp == in + 67);
    // leading space, INT_MAX
    in = " 0b0111111111111111111111111111111111111111111111111111111111111111";
    ASSERT(fn(in, &endp, base) == 0x7fffffffffffffffll && *err == 0 && endp == in + 67);
    // leading +, INT_MAX
    in = "+0b0111111111111111111111111111111111111111111111111111111111111111";
    ASSERT(fn(in, &endp, base) == 0x7fffffffffffffffll && *err == 0 && endp == in + 67);
    // leading -, -INT_MAX
    in = "-0b0111111111111111111111111111111111111111111111111111111111111111";
    ASSERT(fn(in, &endp, base) == (unsigned long)-0x7fffffffffffffffll && *err == 0 && endp == in + 67);
    // leading -, zero
    in = "-0b00";
    ASSERT(fn(in, &endp, base) == 0 && *err == 0 && endp == in + 5);
    // leading -, INT_MIN
    in = "-0b1000000000000000000000000000000000000000000000000000000000000000";
    ASSERT(fn(in, &endp, base) == (unsigned long)-0x7fffffffffffffffll-1 && *err == 0 && endp == in + 67);
    // leading -, INT_MIN - 1
    in = "-0b1000000000000000000000000000000000000000000000000000000000000001";
    ASSERT(fn(in, &endp, base) == (unsigned long)-0x7fffffffffffffffll-2 && *err == 0 && endp == in + 67);
    // leading +, -INT_MIN
    in = "+0b1000000000000000000000000000000000000000000000000000000000000000";
    ASSERT(fn(in, &endp, base) == 0x7fffffffffffffffull+1 && *err == 0 && endp == in + 67);
    // > 2**64
    in = "0b100000000000000000000000000000000000000000000000000000000000000000";
    ASSERT(fn(in, &endp, base) == 0xffffffffffffffffull && *err == ERANGE && endp == in + 68);
    *err = 0;
  }
}

static void test_strtoll_l(void* lib) {
  int* err = NULL;
  long (*fn)(const char*, char**, int, locale_t);
  fn = dlvsym(lib, "__isoc23_strtol_l", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "__isoc23_strtoll_l", "GLIBC_2.38");
    if (!fn) {
      FATAL("Could not find __isoc23_strtoll_l");
    }
    err = dlsym(lib, "errno");
  }
  if (!err) err = &errno;
  locale_t l = newlocale(LC_CTYPE_MASK, "C", NULL);
  // Some simple cases.
  *err = 0;
  for (int base = 0; base <= 10; base += 10) {
    const char* in = "987";
    char* endp = NULL;
    ASSERT(fn(in, &endp, base, l) == 987 && *err == 0 && endp == in + 3);
    in = "  +13x";
    ASSERT(fn(in, &endp, base, l) == 13 && *err == 0 && endp == in + 5);
    in = "  -5b";
    ASSERT(fn(in, &endp, base, l) == -5 && *err == 0 && endp == in + 4);
    in = " 0b2";
    ASSERT(fn(in, &endp, base, l) == 0 && *err == 0 && endp == in + 2);
    if (base == 0) {
      in = " 0xfe";
      ASSERT(fn(in, &endp, base, l) == 254 && *err == 0 && endp == in + 5);
    }
  }
  // Binary parsing
  for (int base = 0; base <= 2; base += 2) {
    const char* in = "0b101";
    char* endp = NULL;
    // Smoke test
    ASSERT(fn(in, &endp, base, l) == 5 && *err == 0 && endp == in + 5);
    // 64 leading zeros
    in = "0b00000000000000000000000000000000000000000000000000000000000000001";
    ASSERT(fn(in, &endp, base, l) == 1 && *err == 0 && endp == in + 67);
    // leading space, INT_MAX
    in = " 0b0111111111111111111111111111111111111111111111111111111111111111";
    ASSERT(fn(in, &endp, base, l) == 0x7fffffffffffffffll && *err == 0 && endp == in + 67);
    // leading +, INT_MAX
    in = "+0b0111111111111111111111111111111111111111111111111111111111111111";
    ASSERT(fn(in, &endp, base, l) == 0x7fffffffffffffffll && *err == 0 && endp == in + 67);
    // leading -, -INT_MAX
    in = "-0b0111111111111111111111111111111111111111111111111111111111111111";
    ASSERT(fn(in, &endp, base, l) == -0x7fffffffffffffffll && *err == 0 && endp == in + 67);
    // leading -, zero
    in = "-0b00";
    ASSERT(fn(in, &endp, base, l) == 0 && *err == 0 && endp == in + 5);
    // leading -, INT_MIN
    in = "-0b1000000000000000000000000000000000000000000000000000000000000000";
    ASSERT(fn(in, &endp, base, l) == -0x7fffffffffffffffll-1 && *err == 0 && endp == in + 67);
    // leading -, INT_MIN - 1
    in = "-0b1000000000000000000000000000000000000000000000000000000000000001";
    ASSERT(fn(in, &endp, base, l) == -0x7fffffffffffffffll-1 && *err == ERANGE && endp == in + 67);
    *err = 0;
    // leading +, -INT_MIN
    in = "+0b1000000000000000000000000000000000000000000000000000000000000000";
    ASSERT(fn(in, &endp, base, l) == 0x7fffffffffffffffll && *err == ERANGE && endp == in + 67);
    *err = 0;
    // > 2**64
    in = "0b100000000000000000000000000000000000000000000000000000000000000000";
    ASSERT(fn(in, &endp, base, l) == 0x7fffffffffffffffll && *err == ERANGE && endp == in + 68);
    *err = 0;
  }
  freelocale(l);
}

static void test_wcstoll(void* lib) {
  int* err = NULL;
  long (*fn)(const wchar_t*, wchar_t**, int);
  fn = dlvsym(lib, "__isoc23_wcstol", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "__isoc23_wcstoll", "GLIBC_2.38");
    if (!fn) {
      FATAL("Could not find __isoc23_wcstoll");
    }
    err = dlsym(lib, "errno");
  }
  if (!err) err = &errno;
  // Some simple cases.
  *err = 0;
  for (int base = 0; base <= 10; base += 10) {
    const wchar_t* in = L"987";
    wchar_t* endp = NULL;
    ASSERT(fn(in, &endp, base) == 987 && *err == 0 && endp == in + 3);
    in = L"  +13x";
    ASSERT(fn(in, &endp, base) == 13 && *err == 0 && endp == in + 5);
    in = L"  -5b";
    ASSERT(fn(in, &endp, base) == -5 && *err == 0 && endp == in + 4);
    in = L" 0b2";
    ASSERT(fn(in, &endp, base) == 0 && *err == 0 && endp == in + 2);
    if (base == 0) {
      in = L" 0xfe";
      ASSERT(fn(in, &endp, base) == 254 && *err == 0 && endp == in + 5);
    }
  }
  // Binary parsing
  for (int base = 0; base <= 2; base += 2) {
    const wchar_t* in = L"0b101";
    wchar_t* endp = NULL;
    // Smoke test
    ASSERT(fn(in, &endp, base) == 5 && *err == 0 && endp == in + 5);
    // 64 leading zeros
    in = L"0b00000000000000000000000000000000000000000000000000000000000000001";
    ASSERT(fn(in, &endp, base) == 1 && *err == 0 && endp == in + 67);
    // leading space, INT_MAX
    in = L" 0b0111111111111111111111111111111111111111111111111111111111111111";
    ASSERT(fn(in, &endp, base) == 0x7fffffffffffffffll && *err == 0 && endp == in + 67);
    // leading +, INT_MAX
    in = L"+0b0111111111111111111111111111111111111111111111111111111111111111";
    ASSERT(fn(in, &endp, base) == 0x7fffffffffffffffll && *err == 0 && endp == in + 67);
    // leading -, -INT_MAX
    in = L"-0b0111111111111111111111111111111111111111111111111111111111111111";
    ASSERT(fn(in, &endp, base) == -0x7fffffffffffffffll && *err == 0 && endp == in + 67);
    // leading -, zero
    in = L"-0b00";
    ASSERT(fn(in, &endp, base) == 0 && *err == 0 && endp == in + 5);
    // leading -, INT_MIN
    in = L"-0b1000000000000000000000000000000000000000000000000000000000000000";
    ASSERT(fn(in, &endp, base) == -0x7fffffffffffffffll-1 && *err == 0 && endp == in + 67);
    // leading -, INT_MIN - 1
    in = L"-0b1000000000000000000000000000000000000000000000000000000000000001";
    ASSERT(fn(in, &endp, base) == -0x7fffffffffffffffll-1 && *err == ERANGE && endp == in + 67);
    *err = 0;
    // leading +, -INT_MIN
    in = L"+0b1000000000000000000000000000000000000000000000000000000000000000";
    ASSERT(fn(in, &endp, base) == 0x7fffffffffffffffll && *err == ERANGE && endp == in + 67);
    *err = 0;
    // > 2**64
    in = L"0b100000000000000000000000000000000000000000000000000000000000000000";
    ASSERT(fn(in, &endp, base) == 0x7fffffffffffffffll && *err == ERANGE && endp == in + 68);
    *err = 0;
  }
}
#endif

static void test_strlcat(void* lib) {
  size_t (*fn)(char*, const char*, size_t);
  fn = dlvsym(lib, "strlcat", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "strlcat", "GLIBC_2.38");
    if (!fn) {
      FATAL("Could not find strlcat");
    }
  }
  char dst[10];
  memset(dst, 'x', sizeof(dst));
  memcpy(dst, "Hi", 3);
  ASSERT(fn(dst, " there", 10) == 8);
  ASSERT(memcmp(dst, "Hi there\0x", 10) == 0);
  ASSERT(fn(dst, "!", 10) == 9);
  ASSERT(memcmp(dst, "Hi there!", 10) == 0);
  ASSERT(fn(dst, "?", 10) == 10);
  ASSERT(memcmp(dst, "Hi there!", 10) == 0);
  ASSERT(fn(dst, "foo", 5) == 8);
  ASSERT(fn(NULL, "foo", 0) == 3);
}

static void test_strlcat_chk(void* lib) {
  size_t (*fn)(char*, const char*, size_t, size_t);
  fn = dlvsym(lib, "__strlcat_chk", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "__strlcat_chk", "GLIBC_2.38");
    if (!fn) {
      FATAL("Could not find __strlcat_chk");
    }
  }
  char dst[10];
  memset(dst, 'x', sizeof(dst));
  memcpy(dst, "Hi", 3);
  ASSERT(fn(dst, " there", 10, 10) == 8);
  ASSERT(memcmp(dst, "Hi there\0x", 10) == 0);
  ASSERT(fn(dst, "!", 10, 10) == 9);
  ASSERT(memcmp(dst, "Hi there!", 10) == 0);
  ASSERT(fn(dst, "?", 10, 10) == 10);
  ASSERT(memcmp(dst, "Hi there!", 10) == 0);
  ASSERT(fn(dst, "foo", 5, 10) == 8);
  ASSERT(fn(NULL, "foo", 0, 10) == 3);
}

static void test_strlcpy(void* lib) {
  size_t (*fn)(char*, const char*, size_t);
  fn = dlvsym(lib, "strlcpy", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "strlcpy", "GLIBC_2.38");
    if (!fn) {
      FATAL("Could not find strlcpy");
    }
  }
  char dst[6];
  memset(dst, 'x', sizeof(dst));
  ASSERT(fn(dst, "Hi there", 6) == 8);
  ASSERT(memcmp(dst, "Hi th", 6) == 0);
  ASSERT(fn(dst, "Hello", 6) == 5);
  ASSERT(memcmp(dst, "Hello", 6) == 0);
  ASSERT(fn(dst, "Hi", 6) == 2);
  ASSERT(memcmp(dst, "Hi\0lo", 6) == 0);
  ASSERT(fn(dst, "Hi", 4) == 2);
  ASSERT(memcmp(dst, "Hi\0lo", 6) == 0);
  ASSERT(fn(NULL, "foo", 0) == 3);
}

static void test_strlcpy_chk(void* lib) {
  size_t (*fn)(char*, const char*, size_t, size_t);
  fn = dlvsym(lib, "__strlcpy_chk", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "__strlcpy_chk", "GLIBC_2.38");
    if (!fn) {
      FATAL("Could not find __strlcpy_chk");
    }
  }
  char dst[6];
  memset(dst, 'x', sizeof(dst));
  ASSERT(fn(dst, "Hi there", 6, 6) == 8);
  ASSERT(memcmp(dst, "Hi th", 6) == 0);
  ASSERT(fn(dst, "Hello", 6, 6) == 5);
  ASSERT(memcmp(dst, "Hello", 6) == 0);
  ASSERT(fn(dst, "Hi", 6, 6) == 2);
  ASSERT(memcmp(dst, "Hi\0lo", 6) == 0);
  ASSERT(fn(dst, "Hi", 4, 6) == 2);
  ASSERT(memcmp(dst, "Hi\0lo", 6) == 0);
  ASSERT(fn(NULL, "foo", 0, 6) == 3);
}

static void test_wcslcat(void* lib) {
  size_t (*fn)(wchar_t*, const wchar_t*, size_t);
  fn = dlvsym(lib, "wcslcat", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "wcslcat", "GLIBC_2.38");
    if (!fn) {
      FATAL("Could not find wcslcat");
    }
  }
  wchar_t dst[10];
  for (int i = 0; i < 10; ++i) dst[i] = L'x';
  memcpy(dst, L"Hi", 3 * sizeof(wchar_t));
  ASSERT(fn(dst, L" there", 10) == 8);
  ASSERT(memcmp(dst, L"Hi there\0x", 10 * sizeof(wchar_t)) == 0);
  ASSERT(fn(dst, L"!", 10) == 9);
  ASSERT(memcmp(dst, L"Hi there!", 10  * sizeof(wchar_t)) == 0);
  ASSERT(fn(dst, L"?", 10) == 10);
  ASSERT(memcmp(dst, L"Hi there!", 10  * sizeof(wchar_t)) == 0);
  ASSERT(fn(dst, L"foo", 5) == 8);
  ASSERT(fn(NULL, L"foo", 0) == 3);
}

static void test_wcslcat_chk(void* lib) {
  size_t (*fn)(wchar_t*, const wchar_t*, size_t, size_t);
  fn = dlvsym(lib, "__wcslcat_chk", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "__wcslcat_chk", "GLIBC_2.38");
    if (!fn) {
      FATAL("Could not find __wcslcat_chk");
    }
  }
  wchar_t dst[10];
  for (int i = 0; i < 10; ++i) dst[i] = L'x';
  memcpy(dst, L"Hi", 3 * sizeof(wchar_t));
  ASSERT(fn(dst, L" there", 10, 10) == 8);
  ASSERT(memcmp(dst, L"Hi there\0x", 10 * sizeof(wchar_t)) == 0);
  ASSERT(fn(dst, L"!", 10, 10) == 9);
  ASSERT(memcmp(dst, L"Hi there!", 10  * sizeof(wchar_t)) == 0);
  ASSERT(fn(dst, L"?", 10, 10) == 10);
  ASSERT(memcmp(dst, L"Hi there!", 10  * sizeof(wchar_t)) == 0);
  ASSERT(fn(dst, L"foo", 5, 10) == 8);
  ASSERT(fn(NULL, L"foo", 0, 10) == 3);
}

static void test_wcslcpy(void* lib) {
  size_t (*fn)(wchar_t*, const wchar_t*, size_t);
  fn = dlvsym(lib, "wcslcpy", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "wcslcpy", "GLIBC_2.38");
    if (!fn) {
      FATAL("Could not find wcslcpy");
    }
  }
  wchar_t dst[6];
  for (int i = 0; i < 6; ++i) dst[i] = L'x';
  ASSERT(fn(dst, L"Hi there", 6) == 8);
  ASSERT(memcmp(dst, L"Hi th", 6 * sizeof(wchar_t)) == 0);
  ASSERT(fn(dst, L"Hello", 6) == 5);
  ASSERT(memcmp(dst, L"Hello", 6 * sizeof(wchar_t)) == 0);
  ASSERT(fn(dst, L"Hi", 6) == 2);
  ASSERT(memcmp(dst, L"Hi\0lo", 6 * sizeof(wchar_t)) == 0);
  ASSERT(fn(dst, L"Hi", 4) == 2);
  ASSERT(memcmp(dst, L"Hi\0lo", 6 * sizeof(wchar_t)) == 0);
  ASSERT(fn(NULL, L"foo", 0) == 3);
}

static void test_wcslcpy_chk(void* lib) {
  size_t (*fn)(wchar_t*, const wchar_t*, size_t, size_t);
  fn = dlvsym(lib, "__wcslcpy_chk", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "__wcslcpy_chk", "GLIBC_2.38");
    if (!fn) {
      FATAL("Could not find __wcslcpy_chk");
    }
  }
  wchar_t dst[6];
  for (int i = 0; i < 6; ++i) dst[i] = L'x';
  ASSERT(fn(dst, L"Hi there", 6, 6) == 8);
  ASSERT(memcmp(dst, L"Hi th", 6 * sizeof(wchar_t)) == 0);
  ASSERT(fn(dst, L"Hello", 6, 6) == 5);
  ASSERT(memcmp(dst, L"Hello", 6 * sizeof(wchar_t)) == 0);
  ASSERT(fn(dst, L"Hi", 6, 6) == 2);
  ASSERT(memcmp(dst, L"Hi\0lo", 6 * sizeof(wchar_t)) == 0);
  ASSERT(fn(dst, L"Hi", 4, 6) == 2);
  ASSERT(memcmp(dst, L"Hi\0lo", 6 * sizeof(wchar_t)) == 0);
  ASSERT(fn(NULL, L"foo", 0, 6) == 3);
}

int main(int argc, const char** argv) {
  ASSERT(argc >= 2);
  void* lib = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
  if (!lib) FATAL("Could not dlopen %s", argv[1]);
#ifndef TEST_AARCH64
  test_strtoll(lib);
  test_strtoull(lib);
  test_strtoll_l(lib);
  test_wcstoll(lib);
#endif
  test_strlcat(lib);
  test_strlcat_chk(lib);
  test_strlcpy(lib);
  test_strlcpy_chk(lib);
  test_wcslcat(lib);
  test_wcslcat_chk(lib);
  test_wcslcpy(lib);
  test_wcslcpy_chk(lib);
  FILE* f = argv[2] ? fopen(argv[2], "w") : stdout;
  fputs("OK\n", f);
  fclose(f);
  return 0;
}
