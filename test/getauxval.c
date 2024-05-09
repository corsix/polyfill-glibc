#include <sys/auxv.h>
#include <errno.h>
#include <string.h>
#include "../src/common.h"

int main(int argc, const char** argv) {
  ASSERT(argc >= 3);
  char* imports_before = read_entire_file(argv[1], NULL);
  char* imports_after = read_entire_file(argv[2], NULL);
  ASSERT(strstr(imports_before, "getauxval"));
  ASSERT(strstr(imports_before, "libc"));
  ASSERT(strstr(imports_before, "GLIBC_2.16"));
  ASSERT(!strstr(imports_after, "getauxval"));
  ASSERT(strstr(imports_after, "libc"));
  ASSERT(!strstr(imports_after, "GLIBC_2.16"));
  free(imports_before);
  free(imports_after);

  unsigned long phent = getauxval(AT_PHENT);
  ASSERT(phent == 32 || phent == 56);
  void* vdso = (void*)getauxval(AT_SYSINFO_EHDR);
  ASSERT(vdso != NULL);
  ASSERT(memcmp(vdso, "\177ELF", 4) == 0);
  errno = 0;
  ASSERT(getauxval(0xCAFE1234) == 0);
  ASSERT(errno == ENOENT);

  FILE* f = argv[3] ? fopen(argv[3], "w") : stdout;
  fputs("OK\n", f);
  fclose(f);
  return 0;
}
