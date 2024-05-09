#include <dlfcn.h>
#include <errno.h>
#include <locale.h>
#include "../../src/common.h"

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

int main(int argc, const char** argv) {
  ASSERT(argc >= 2);
  void* lib = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
  if (!lib) FATAL("Could not dlopen %s", argv[1]);
  test_strtoll(lib);
  test_strtoull(lib);
  test_strtoll_l(lib);
  test_wcstoll(lib);
  FILE* f = argv[2] ? fopen(argv[2], "w") : stdout;
  fputs("OK\n", f);
  fclose(f);
  return 0;
}
